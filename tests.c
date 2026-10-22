#include "parser.h"
#include "csv.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define current_directory _getcwd
#define change_directory _chdir
#else
#include <unistd.h>
#define current_directory getcwd
#define change_directory chdir
#endif

static unsigned checks, failures;
#define CHECK(expression) do { ++checks; if (!(expression)) { ++failures; fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expression); } } while (0)

static bool execute(Database *db, const char *sql, bool expected, const char *contains) {
    Query query;
    char error[DB_ERROR_SIZE], output[8192];
    FILE *file = tmpfile();
    bool ok;
    size_t length;
    if (!file) { CHECK(file != NULL); return false; }
    ok = parse_query(sql, &query, error) && db_execute(db, &query, file, error);
    CHECK(ok == expected);
    if (ok != expected) fprintf(stderr, "SQL: %s\nError: %s\n", sql, error);
    CHECK(fflush(file) == 0);
    rewind(file);
    length = fread(output, 1, sizeof output - 1, file);
    output[length] = '\0';
    if (contains) CHECK(strstr(ok ? output : error, contains) != NULL);
    CHECK(fclose(file) == 0);
    return ok;
}
static void put_file(const char *path, const char *text) {
    FILE *file = fopen(path, "wb");
    CHECK(file != NULL);
    if (!file) return;
    CHECK(fwrite(text, 1, strlen(text), file) == strlen(text));
    CHECK(fclose(file) == 0);
}
static unsigned long fingerprint(const char *path) {
    FILE *file = fopen(path, "rb");
    unsigned long hash = 5381;
    int c;
    CHECK(file != NULL);
    if (!file) return 0;
    while ((c = fgetc(file)) != EOF) hash = hash * 33u + (unsigned char)c;
    CHECK(!ferror(file)); CHECK(fclose(file) == 0);
    return hash;
}
static void csv_tests(void) {
    char error[DB_ERROR_SIZE];
    CsvRecord original = {0}, decoded;
    FILE *file = tmpfile();
    CHECK(file != NULL);
    if (!file) return;
    original.count = 5;
    strcpy(original.fields[0], "a,b");
    strcpy(original.fields[1], "say \"hello\"");
    strcpy(original.fields[2], "line1\r\nline2\nlast");
    strcpy(original.fields[3], "");
    memset(original.fields[4], 'x', DB_MAX_STRING);
    CHECK(csv_write_record(file, &original, error));
    CHECK(fflush(file) == 0); rewind(file);
    CHECK(csv_read_record(file, &decoded, error) == 1);
    CHECK(decoded.count == original.count);
    for (size_t i = 0; i < original.count; ++i) CHECK(!strcmp(original.fields[i], decoded.fields[i]));
    CHECK(csv_read_record(file, &decoded, error) == 0);
    CHECK(fclose(file) == 0);
    {
        const char *bad[] = {"\"unterminated", "a\"b\n", "\"a\"z\n", "a,\0b\n"};
        for (size_t i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
            file = tmpfile(); CHECK(file != NULL); if (!file) continue;
            CHECK(fwrite(bad[i], 1, i == 3 ? 5 : strlen(bad[i]), file) == (i == 3 ? 5 : strlen(bad[i])));
            rewind(file); CHECK(csv_read_record(file, &decoded, error) == -1); CHECK(fclose(file) == 0);
        }
    }
    file = tmpfile(); CHECK(file != NULL);
    if (file) {
        fputs("a,b\r\n\"\",last", file); rewind(file);
        CHECK(csv_read_record(file, &decoded, error) == 1 && decoded.count == 2);
        CHECK(csv_read_record(file, &decoded, error) == 1 && !strcmp(decoded.fields[0], "") && !strcmp(decoded.fields[1], "last"));
        CHECK(csv_read_record(file, &decoded, error) == 0); CHECK(fclose(file) == 0);
    }
    file = tmpfile(); CHECK(file != NULL);
    if (file) {
        for (int i = 0; i <= DB_MAX_STRING; ++i) fputc('x', file);
        rewind(file); CHECK(csv_read_record(file, &decoded, error) == -1); CHECK(fclose(file) == 0);
    }
    file = tmpfile(); CHECK(file != NULL);
    if (file) {
        for (int i = 0; i < DB_MAX_COLUMNS; ++i) fputs("x,", file);
        fputs("x\n", file); rewind(file);
        CHECK(csv_read_record(file, &decoded, error) == -1); CHECK(fclose(file) == 0);
    }
}
static void parser_tests(void) {
    Query query;
    Lexer lexer;
    Token token;
    char error[DB_ERROR_SIZE];
    const char *bad[] = {
        "", "SELECT * FROM t", "SELECT FROM t;", "SELECT * FROM ../t;",
        "CREATE TABLE t ();", "CREATE TABLE t (a NOPE);", "INSERT INTO t VALUES ();",
        "INSERT INTO t VALUES (2147483648);", "INSERT INTO t VALUES (-2147483649);",
        "INSERT INTO t VALUES (1e999);", "INSERT INTO t VALUES (1e-999);",
        "INSERT INTO t VALUES (12x);", "INSERT INTO t VALUES (1.2.3);",
        "INSERT INTO t VALUES ('unclosed);", "SELECT * FROM t WHERE a ! 2;",
        "SELECT * FROM t WHERE a = 1 AND b = 2;", "SELECT * FROM t ORDER BY a;",
        "UPDATE t SET a = 1, b = 2;", "SELECT * FROM t; DROP TABLE t;",
        "CREATE TABLE t (a INT,);", "SELECT * FROM t WHERE a = NULL;"
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; ++i) CHECK(!parse_query(bad[i], &query, error));
    CHECK(parse_query("InSeRt INTO T VALUES (-2147483648, +2147483647, -.5e+2, 'O''Brien;X', TRUE);", &query, error));
    CHECK(!strcmp(query.table, "t") && query.value_count == 5);
    CHECK(query.values[0].as.integer == INT32_MIN && query.values[1].as.integer == INT32_MAX);
    CHECK(query.values[2].as.floating == -50 && !strcmp(query.values[3].as.string, "O'Brien;X"));
    lexer_init(&lexer, "SELECT name, 1.5 != 2;");
    CHECK(lexer_next(&lexer, &token, error) && token.kind == TOK_SELECT);
    CHECK(lexer_next(&lexer, &token, error) && token.kind == TOK_IDENTIFIER && !strcmp(token.text, "name"));
    {
        char long_sql[DB_MAX_QUERY + 2];
        memset(long_sql, ' ', sizeof long_sql - 1); long_sql[sizeof long_sql - 1] = '\0';
        CHECK(!parse_query(long_sql, &query, error));
    }
    {
        char sql[512], literal[257];
        memset(literal, 'x', 256); literal[256] = '\0';
        snprintf(sql, sizeof sql, "INSERT INTO t VALUES ('%s');", literal);
        CHECK(!parse_query(sql, &query, error));
        literal[255] = '\0'; snprintf(sql, sizeof sql, "INSERT INTO t VALUES ('%s');", literal);
        CHECK(parse_query(sql, &query, error));
        snprintf(sql, sizeof sql, "CREATE TABLE %s (a INT);", literal);
        CHECK(!parse_query(sql, &query, error));
    }
    {
        char sql[2048] = "CREATE TABLE wide (";
        char column[32];
        for (int i = 0; i < DB_MAX_COLUMNS; ++i) {
            snprintf(column, sizeof column, "%sc%d INT", i ? "," : "", i); strcat(sql, column);
        }
        strcat(sql, ");"); CHECK(parse_query(sql, &query, error));
        sql[strlen(sql) - 2] = '\0'; strcat(sql, ",extra INT);");
        CHECK(!parse_query(sql, &query, error));
    }
}
static void engine_tests(Database *db) {
    char error[DB_ERROR_SIZE], path[640], schema[640], temp[640];
    unsigned long before;
    Database restarted;
    execute(db, "CREATE TABLE Students (id INT, name STRING, cgpa FLOAT, active BOOL);", true, "created");
    execute(db, "CREATE TABLE students (id INT);", false, "already exists");
    execute(db, "CREATE TABLE duplicate (id INT, ID STRING);", false, "duplicate");
    execute(db, "INSERT INTO students VALUES (1, 'Aman', 8.5, true);", true, "1 row inserted");
    execute(db, "INSERT INTO students VALUES (2, 'O''Brien, \"Alex\"', 7, false);", true, NULL);
    execute(db, "INSERT INTO students VALUES (3, 'line1\nline2', 9.25, true);", true, NULL);
    execute(db, "INSERT INTO students VALUES (-2147483648, '', 1e2, false);", true, NULL);
    execute(db, "SELECT * FROM STUDENTS;", true, "4 rows returned");
    execute(db, "SELECT name, cgpa FROM students WHERE id = 2;", true, "O'Brien, \"Alex\"");
    execute(db, "SELECT name FROM students WHERE id = 3;", true, "line1\\nline2");
    execute(db, "SELECT id FROM students WHERE cgpa > 8;", true, "3 rows returned");
    execute(db, "SELECT id FROM students WHERE id != 1;", true, "3 rows returned");
    execute(db, "SELECT id FROM students WHERE id < 2;", true, "2 rows returned");
    execute(db, "SELECT id FROM students WHERE id <= 2;", true, "3 rows returned");
    execute(db, "SELECT id FROM students WHERE id >= 2.0;", true, "2 rows returned");
    execute(db, "SELECT name FROM students WHERE name > 'Aman';", true, "2 rows returned");
    execute(db, "SELECT id FROM students WHERE active = true;", true, "2 rows returned");
    execute(db, "SELECT id FROM students WHERE active != true;", true, "2 rows returned");
    execute(db, "SELECT * FROM students WHERE id = 999;", true, "0 rows returned");
    snprintf(path, sizeof path, "%s/students.csv", db->directory);
    before = fingerprint(path);
    execute(db, "INSERT INTO students VALUES ('bad', 'x', 1, true);", false, "Type mismatch");
    execute(db, "INSERT INTO students VALUES (1, 'x', 1, 1);", false, "Type mismatch");
    execute(db, "INSERT INTO students VALUES (1.0, 'x', 1, true);", false, "Type mismatch");
    execute(db, "INSERT INTO students VALUES (1);", false, "Expected 4 values");
    execute(db, "UPDATE students SET id = 'wrong';", false, "Type mismatch");
    execute(db, "UPDATE students SET absent = 2;", false, "Unknown update column");
    execute(db, "SELECT absent FROM students;", false, "Unknown selected column");
    execute(db, "DELETE FROM students WHERE absent = 2;", false, "Unknown WHERE column");
    execute(db, "SELECT * FROM students WHERE active > false;", false, "BOOL supports only");
    execute(db, "SELECT * FROM students WHERE id = '1';", false, "Incompatible WHERE");
    CHECK(fingerprint(path) == before);
    execute(db, "UPDATE students SET cgpa = 9.2 WHERE id = 1;", true, "1 row updated");
    execute(db, "SELECT id FROM students WHERE cgpa = 9.2;", true, "1 row returned");
    execute(db, "UPDATE students SET cgpa = 9 WHERE id = 999;", true, "0 rows updated");
    CHECK(db_init(&restarted, db->directory, error));
    execute(&restarted, "SELECT name FROM students WHERE id = 2;", true, "O'Brien");
    execute(db, "DELETE FROM students WHERE id = 2;", true, "1 row deleted");
    execute(db, "DELETE FROM students WHERE id = 999;", true, "0 rows deleted");
    execute(db, "UPDATE students SET active = false;", true, "3 rows updated");
    execute(db, "SELECT * FROM students WHERE active = true;", true, "0 rows returned");
    snprintf(temp, sizeof temp, "%s/students.csv.tmp", db->directory);
    put_file(temp, "recovery data"); before = fingerprint(path);
    execute(db, "DELETE FROM students;", false, "temporary file");
    CHECK(fingerprint(path) == before); CHECK(remove(temp) == 0);
    execute(db, "DELETE FROM students;", true, "3 rows deleted");
    execute(db, "SELECT * FROM students;", true, "0 rows returned");
    execute(db, "DROP TABLE students;", true, "dropped");
    { struct stat info; CHECK(stat(path, &info) != 0); }
    execute(db, "SELECT * FROM students;", false, "schema");
    execute(db, "INSERT INTO missing VALUES (1);", false, "schema");
    execute(db, "UPDATE missing SET id = 1;", false, "schema");
    execute(db, "DELETE FROM missing;", false, "schema");
    execute(db, "DROP TABLE missing;", false, "schema");

    execute(db, "CREATE TABLE damaged (id INT, name STRING);", true, NULL);
    snprintf(path, sizeof path, "%s/damaged.csv", db->directory);
    snprintf(schema, sizeof schema, "%s/damaged.schema", db->directory);
    {
        const char *bad[] = {"id,name\n1\n", "wrong,name\n1,x\n", "id,name\nx,y\n", "id,name\n1,\"unterminated", "id,name\n1,ok,extra\n"};
        for (size_t i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
            put_file(path, bad[i]); before = fingerprint(path);
            execute(db, "INSERT INTO damaged VALUES (2, 'x');", false, NULL);
            execute(db, "UPDATE damaged SET name = 'x';", false, NULL);
            execute(db, "DELETE FROM damaged;", false, NULL);
            CHECK(fingerprint(path) == before);
        }
    }
    put_file(path, "id,name\n1,ok\n");
    {
        const char *bad[] = {"id:INT\nid:STRING\n", "../id:INT\n", "id:NOPE\n", "", "id:INT\nname:STRING\nextra:BOOL\n"};
        for (size_t i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
            put_file(schema, bad[i]); before = fingerprint(path);
            execute(db, "DELETE FROM damaged;", false, NULL); CHECK(fingerprint(path) == before);
        }
    }
    put_file(schema, "id:INT\nname:STRING\n");
    {
        const char binary_schema[] = "id:INT\0hidden\nname:STRING\n";
        FILE *file = fopen(schema, "wb"); CHECK(file != NULL);
        if (file) {
            CHECK(fwrite(binary_schema, 1, sizeof binary_schema - 1, file) == sizeof binary_schema - 1);
            CHECK(fclose(file) == 0);
            execute(db, "DELETE FROM damaged;", false, "NUL byte");
        }
    }
    put_file(schema, "id:INT\nname:STRING\n");
    execute(db, "DROP TABLE damaged;", true, NULL);

    execute(db, "CREATE TABLE fulltable (id INT);", true, NULL);
    snprintf(path, sizeof path, "%s/fulltable.csv", db->directory);
    {
        FILE *file = fopen(path, "wb"); CHECK(file != NULL);
        if (file) {
            fputs("id\n", file);
            for (int i = 0; i < DB_MAX_ROWS; ++i) fprintf(file, "%d\n", i);
            CHECK(fclose(file) == 0);
        }
        before = fingerprint(path);
        execute(db, "INSERT INTO fulltable VALUES (10000);", false, "Maximum");
        CHECK(fingerprint(path) == before);
        execute(db, "DELETE FROM fulltable WHERE id >= 1;", true, "9999 rows deleted");
        execute(db, "DROP TABLE fulltable;", true, NULL);
    }
}

static void read_text(const char *path, char *text, size_t capacity) {
    FILE *file = fopen(path, "rb");
    size_t length;
    CHECK(file != NULL);
    text[0] = '\0';
    if (!file) return;
    length = fread(text, 1, capacity - 1, file);
    text[length] = '\0';
    CHECK(!ferror(file)); CHECK(fclose(file) == 0);
}
static void cli_run_bytes(const char *executable, const char *sql, size_t sql_length, bool success, const char *output_part, const char *error_part) {
    char command[1400], output[8192], error[8192];
    int status;
    FILE *input = fopen("input.sql", "wb");
    CHECK(input != NULL);
    if (!input) return;
    CHECK(fwrite(sql, 1, sql_length, input) == sql_length);
    CHECK(fclose(input) == 0);
#ifdef _WIN32
    /* cmd.exe needs an outer quote pair around a quoted executable plus redirection. */
    snprintf(command, sizeof command, "\"\"%s\" < input.sql > stdout.txt 2> stderr.txt\"", executable);
#else
    snprintf(command, sizeof command, "\"%s\" < input.sql > stdout.txt 2> stderr.txt", executable);
#endif
    status = system(command);
    CHECK((status == 0) == success);
    read_text("stdout.txt", output, sizeof output);
    read_text("stderr.txt", error, sizeof error);
    if (output_part) CHECK(strstr(output, output_part) != NULL);
    if (error_part) CHECK(strstr(error, error_part) != NULL);
    if ((status == 0) != success) fprintf(stderr, "Child process output: %s\nErrors: %s\n", output, error);
}
static void cli_run(const char *executable, const char *sql, bool success, const char *output_part, const char *error_part) {
    cli_run_bytes(executable, sql, strlen(sql), success, output_part, error_part);
}
static void cli_tests(const char *directory) {
    char original[1024], executable[1200];
    char *oversized;
    if (!current_directory(original, sizeof original)) { CHECK(false); return; }
#ifdef _WIN32
    snprintf(executable, sizeof executable, "%s\\minireldb.exe", original);
#else
    snprintf(executable, sizeof executable, "%s/minireldb", original);
#endif
    if (change_directory(directory) != 0) { CHECK(false); return; }
    cli_run(executable, "CREATE TABLE persisted (id INT, name STRING, score FLOAT, active BOOL); INSERT INTO persisted VALUES (1, 'O''Brien; comma, quote \"yes\"\nsecond line', 8.5, true);", true, "1 row inserted", NULL);
    cli_run(executable, "SELECT * FROM persisted;", true, "O'Brien; comma, quote \"yes\"\\nsecond line", NULL);
    cli_run(executable, "UPDATE persisted SET score = 9.2 WHERE id = 1; SELECT id FROM persisted WHERE score = 9.2;", true, "1 row returned", NULL);
    cli_run(executable, "INSERT INTO persisted VALUES ('bad', 'x', 1, true); SELECT * FROM persisted;", false, "1 row returned", "Type mismatch");
    cli_run(executable, "SELECT * FROM persisted", false, NULL, "semicolon required");
    {
        const char binary[] = "INSERT INTO persisted VALUES (2, 'bad\0data', 1, true); SELECT * FROM persisted;";
        cli_run_bytes(executable, binary, sizeof binary - 1, false, "1 row returned", "NUL byte");
    }
    oversized = malloc(DB_MAX_QUERY + 100);
    CHECK(oversized != NULL);
    if (oversized) {
        memset(oversized, 'x', DB_MAX_QUERY + 1);
        strcpy(oversized + DB_MAX_QUERY + 1, "; SELECT * FROM persisted;");
        cli_run(executable, oversized, false, "1 row returned", "exceeds 64 KiB");
        free(oversized);
    }
    cli_run(executable, "DELETE FROM persisted; DROP TABLE persisted; .help\n.exit\n", true, "Show help", NULL);
    {
        char example_path[1200], sql[8192];
        snprintf(example_path, sizeof example_path, "%s/examples/demo.sql", original);
        read_text(example_path, sql, sizeof sql);
        cli_run(executable, sql, true, "Table 'students' dropped", NULL);
        snprintf(example_path, sizeof example_path, "%s/examples/errors.sql", original);
        read_text(example_path, sql, sizeof sql);
        cli_run(executable, sql, false, "Table 'validation' dropped", "Type mismatch");
    }
    CHECK(change_directory(original) == 0);
}

int main(void) {
    Database db, base;
    char error[DB_ERROR_SIZE], directory[512];
    struct stat info;
    unsigned attempt;
    if (!db_init(&base, "test-output", error)) { fprintf(stderr, "%s\n", error); return EXIT_FAILURE; }
    for (attempt = 0; attempt < 1000; ++attempt) {
        snprintf(directory, sizeof directory, "test-output/run-%lld-%u", (long long)time(NULL), attempt);
        if (stat(directory, &info) != 0) break;
    }
    if (attempt == 1000 || !db_init(&db, directory, error)) { fprintf(stderr, "Cannot create test directory\n"); return EXIT_FAILURE; }
    csv_tests(); parser_tests(); engine_tests(&db); cli_tests(directory);
    printf("%u checks, %u failures\n", checks, failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
