#include "parser.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#define input_is_terminal() _isatty(_fileno(stdin))
#else
#include <unistd.h>
#define input_is_terminal() isatty(STDIN_FILENO)
#endif

static const char *help =
    "CREATE TABLE students (id INT, name STRING, cgpa FLOAT, active BOOL);\n"
    "INSERT INTO students VALUES (1, 'O''Brien', 8.5, true);\n"
    "SELECT * FROM students WHERE cgpa >= 8;\n"
    "SELECT name, cgpa FROM students;\n"
    "UPDATE students SET cgpa = 9.2 WHERE id = 1;\n"
    "DELETE FROM students WHERE id = 1;\n"
    "DROP TABLE students;\n"
    "WHERE supports one condition: = != < > <= >=\n"
    "UPDATE/DELETE without WHERE affect every row.\n"
    "End SQL with ;. Double apostrophes inside strings.\n"
    ".help  Show help\n.exit  Exit\n";

int main(int argc, char **argv) {
    Database db;
    char error[DB_ERROR_SIZE], *sql;
    size_t length = 0;
    bool quoted = false, bad_input = false, any_failure = false, has_text = false;
    bool interactive = argc == 1 && input_is_terminal();
    FILE *input = stdin;
    int c;
    if (argc > 2) { fprintf(stderr, "Usage: %s [script.sql]\n", argv[0]); return EXIT_FAILURE; }
    if (argc == 2) {
        input = fopen(argv[1], "rb");
        if (!input) { perror("Cannot open SQL script"); return EXIT_FAILURE; }
    }
    if (!db_init(&db, "database", error)) { fprintf(stderr, "Error: %s\n", error); if (input != stdin) fclose(input); return EXIT_FAILURE; }
    sql = malloc(DB_MAX_QUERY + 1);
    if (!sql) { fprintf(stderr, "Error: out of memory\n"); if (input != stdin) fclose(input); return EXIT_FAILURE; }
    if (interactive) {
        puts("====================================\n       MiniRelDB v1.0\n     CSV-Based SQL Engine\n====================================\n\nType .help for available commands.");
        printf("minireldb> "); fflush(stdout);
    }
    while ((c = fgetc(input)) != EOF) {
        if (!has_text && !bad_input && c == '.') {
            char command[32];
            size_t n = 0;
            bool too_long = false;
            command[n++] = '.';
            while ((c = fgetc(input)) != EOF && c != '\n') {
                if (n + 1 < sizeof command) command[n++] = (char)c; else too_long = true;
            }
            while (n && isspace((unsigned char)command[n - 1])) --n;
            command[n] = '\0'; length = 0;
            if (!too_long && !strcmp(command, ".exit")) { if (interactive) puts("Goodbye!"); break; }
            if (!too_long && !strcmp(command, ".help")) fputs(help, stdout);
            else { fprintf(stderr, "Error: unknown terminal command\n"); any_failure = true; }
            if (interactive) { printf("minireldb> "); fflush(stdout); }
            continue;
        }
        if (c == 0 || length == DB_MAX_QUERY) {
            if (!bad_input) fprintf(stderr, "Error: NUL byte or statement exceeds 64 KiB\n");
            bad_input = any_failure = true;
        }
        if (!bad_input) {
            sql[length++] = (char)c;
            if (!isspace((unsigned char)c)) has_text = true;
            /* Discard leading whitespace so blank lines do not consume the limit. */
            if (!has_text) length = 0;
        }
        if (c == '\'') quoted = !quoted;
        if (c == ';' && !quoted) {
            if (!bad_input) {
                Query query;
                sql[length] = '\0';
                if (!parse_query(sql, &query, error) || !db_execute(&db, &query, stdout, error)) {
                    fprintf(stderr, "Error: %s\n", error); any_failure = true;
                }
            }
            length = 0; bad_input = has_text = false;
            if (interactive) { printf("minireldb> "); fflush(stdout); }
        } else if (c == '\n' && interactive && has_text) { printf("       ... "); fflush(stdout); }
    }
    if (has_text && !bad_input) { fprintf(stderr, "Error: incomplete statement at EOF; terminating semicolon required\n"); any_failure = true; }
    if (ferror(input)) { fprintf(stderr, "Error: input read failed\n"); any_failure = true; }
    if (input != stdin && fclose(input) != 0) any_failure = true;
    if (fflush(stdout) != 0) any_failure = true;
    free(sql);
    return any_failure ? EXIT_FAILURE : EXIT_SUCCESS;
}
