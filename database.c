#include "database.h"
#include "parser.h"
#include "csv.h"

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

typedef struct { Value values[DB_MAX_COLUMNS]; } Row;
typedef struct {
    Column columns[DB_MAX_COLUMNS];
    size_t column_count, row_count, capacity;
    Row *rows;
} Table;

static bool failure(char *error, const char *format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(error, DB_ERROR_SIZE, format, args);
    va_end(args);
    return false;
}
const char *db_type_name(DataType type) {
    static const char *names[] = {"INT", "FLOAT", "STRING", "BOOL"};
    return type >= TYPE_INT && type <= TYPE_BOOL ? names[type] : "INVALID";
}
static bool valid_name(const char *name) {
    Lexer lexer;
    Token token;
    char error[DB_ERROR_SIZE];
    lexer_init(&lexer, name);
    return lexer_next(&lexer, &token, error) && token.kind == TOK_IDENTIFIER &&
           !strcmp(name, token.text) && lexer_next(&lexer, &token, error) && token.kind == TOK_END;
}
static bool path_for(const Database *db, const char *name, const char *suffix, char *path, char *error) {
    if (!valid_name(name)) return failure(error, "Invalid table name");
    int count = snprintf(path, 640, "%s/%s%s", db->directory, name, suffix);
    return (count >= 0 && count < 640) || failure(error, "Database path too long");
}
bool db_init(Database *db, const char *directory, char *error) {
    struct stat info;
    if (!*directory || strlen(directory) >= sizeof db->directory) return failure(error, "Invalid database directory");
    strcpy(db->directory, directory);
    if (stat(directory, &info) == 0) {
#ifdef _WIN32
        if (!(info.st_mode & _S_IFDIR)) return failure(error, "Database path is not a directory");
#else
        if (!S_ISDIR(info.st_mode)) return failure(error, "Database path is not a directory");
#endif
        return true;
    }
#ifdef _WIN32
    if (_mkdir(directory) != 0)
#else
    if (mkdir(directory, 0700) != 0)
#endif
        return failure(error, "Cannot create database directory: %s", strerror(errno));
    return true;
}
static int column_index(const Table *table, const char *name) {
    for (size_t i = 0; i < table->column_count; ++i)
        if (!strcmp(table->columns[i].name, name)) return (int)i;
    return -1;
}
static bool add_row(Table *table, const Row *row, char *error) {
    if (table->row_count == DB_MAX_ROWS) return failure(error, "Maximum 10000 rows per table");
    if (table->row_count == table->capacity) {
        size_t capacity = table->capacity ? table->capacity * 2 : 16;
        Row *rows;
        if (capacity > DB_MAX_ROWS) capacity = DB_MAX_ROWS;
        rows = realloc(table->rows, capacity * sizeof *rows);
        if (!rows) return failure(error, "Out of memory");
        table->rows = rows; table->capacity = capacity;
    }
    table->rows[table->row_count++] = *row;
    return true;
}
static bool load_schema(const char *path, Table *table, char *error) {
    FILE *file = fopen(path, "rb");
    char line[DB_MAX_NAME + 16];
    bool ok = true;
    if (!file) return failure(error, "Cannot open table schema: %s", strerror(errno));
    for (;;) {
        size_t length = 0;
        int byte;
        char *colon;
        Column column;
        while ((byte = fgetc(file)) != EOF && byte != '\n') {
            if (!byte || length + 1 == sizeof line) { ok = failure(error, "NUL byte or oversized schema line"); break; }
            line[length++] = (char)byte;
        }
        if (!ok) break;
        if (!length && byte == EOF) break;
        line[length] = '\0';
        while (length && (line[length - 1] == '\n' || line[length - 1] == '\r')) line[--length] = '\0';
        colon = strchr(line, ':');
        if (!colon) { ok = failure(error, "Invalid schema: expected name:TYPE"); break; }
        *colon++ = '\0';
        if (!valid_name(line) || column_index(table, line) >= 0 || table->column_count == DB_MAX_COLUMNS) {
            ok = failure(error, "Invalid schema column, duplicate name, or too many columns"); break;
        }
        strcpy(column.name, line);
        if (!strcmp(colon, "INT")) column.type = TYPE_INT;
        else if (!strcmp(colon, "FLOAT")) column.type = TYPE_FLOAT;
        else if (!strcmp(colon, "STRING")) column.type = TYPE_STRING;
        else if (!strcmp(colon, "BOOL")) column.type = TYPE_BOOL;
        else { ok = failure(error, "Invalid schema type '%s'", colon); break; }
        table->columns[table->column_count++] = column;
    }
    if (ferror(file)) ok = failure(error, "Schema read failed");
    if (fclose(file) != 0) ok = failure(error, "Schema close failed");
    if (ok && !table->column_count) ok = failure(error, "Schema has no columns");
    return ok;
}
static bool stored_value(const char *text, DataType type, Value *value, char *error) {
    Lexer lexer;
    Token token, end_token;
    char *end;
    value->type = type;
    if (type == TYPE_STRING) { strcpy(value->as.string, text); return true; }
    if (type == TYPE_BOOL) {
        if (strcmp(text, "true") && strcmp(text, "false")) return failure(error, "Invalid stored BOOL");
        value->as.boolean = !strcmp(text, "true"); return true;
    }
    lexer_init(&lexer, text);
    if (!*text || !lexer_next(&lexer, &token, error) ||
        (token.kind != TOK_INTEGER && token.kind != TOK_FLOAT) ||
        !lexer_next(&lexer, &end_token, error) || end_token.kind != TOK_END)
        return failure(error, "Invalid stored numeric value");
    errno = 0;
    if (type == TYPE_INT) {
        long long number = strtoll(text, &end, 10);
        if (token.kind != TOK_INTEGER || errno || *end || number < INT32_MIN || number > INT32_MAX)
            return failure(error, "Invalid stored INT");
        value->as.integer = (int32_t)number;
    } else {
        value->as.floating = strtod(text, &end);
        if (errno || *end || !isfinite(value->as.floating)) return failure(error, "Invalid stored FLOAT");
    }
    return true;
}
static bool load_table(const Database *db, const char *name, Table *table, char *error) {
    char path[640];
    CsvRecord record;
    FILE *file;
    int result;
    bool ok = true;
    if (!path_for(db, name, ".schema", path, error) || !load_schema(path, table, error) ||
        !path_for(db, name, ".csv", path, error)) return false;
    file = fopen(path, "rb");
    if (!file) return failure(error, "Cannot open table CSV: %s", strerror(errno));
    result = csv_read_record(file, &record, error);
    if (result != 1 || record.count != table->column_count) ok = failure(error, "CSV header missing, malformed, or mismatched");
    if (ok) for (size_t i = 0; i < record.count; ++i) {
        if (strcmp(record.fields[i], table->columns[i].name)) { ok = failure(error, "CSV header does not match schema"); break; }
    }
    while (ok && (result = csv_read_record(file, &record, error)) == 1) {
        Row row = {0};
        if (record.count != table->column_count) { ok = failure(error, "Stored record has wrong number of fields"); break; }
        for (size_t i = 0; i < record.count; ++i) {
            if (!stored_value(record.fields[i], table->columns[i].type, &row.values[i], error)) { ok = false; break; }
        }
        if (ok) ok = add_row(table, &row, error);
    }
    if (ok && result < 0) ok = false;
    if (fclose(file) != 0) ok = failure(error, "CSV close failed");
    return ok;
}
static void value_text(const Value *value, char *text) {
    switch (value->type) {
        case TYPE_INT: snprintf(text, DB_MAX_STRING + 1, "%" PRId32, value->as.integer); break;
        case TYPE_FLOAT: snprintf(text, DB_MAX_STRING + 1, "%.17g", value->as.floating); break;
        case TYPE_STRING: strcpy(text, value->as.string); break;
        case TYPE_BOOL: strcpy(text, value->as.boolean ? "true" : "false"); break;
    }
}
static bool write_table(FILE *file, const Table *table, char *error) {
    CsvRecord record;
    record.count = table->column_count;
    for (size_t i = 0; i < record.count; ++i) strcpy(record.fields[i], table->columns[i].name);
    if (!csv_write_record(file, &record, error)) return false;
    for (size_t r = 0; r < table->row_count; ++r) {
        for (size_t c = 0; c < record.count; ++c) value_text(&table->rows[r].values[c], record.fields[c]);
        if (!csv_write_record(file, &record, error)) return false;
    }
    return true;
}
static bool save_table(const Database *db, const char *name, const Table *table, char *error) {
    char path[640], temporary[640];
    FILE *file;
    bool ok;
    if (!path_for(db, name, ".csv", path, error) || !path_for(db, name, ".csv.tmp", temporary, error)) return false;
    /* Exclusive creation also prevents silently overwriting a stale recovery file. */
    file = fopen(temporary, "wbx");
    if (!file) return failure(error, "Cannot create temporary file (check for a stale .csv.tmp): %s", strerror(errno));
    ok = write_table(file, table, error);
    if (fclose(file) != 0) ok = failure(error, "Temporary file close failed");
    if (!ok) { remove(temporary); return false; }
#ifdef _WIN32
    if (!MoveFileExA(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        unsigned long code = GetLastError();
        remove(temporary);
        return failure(error, "Cannot replace CSV (Windows error %lu); original preserved", code);
    }
#else
    if (rename(temporary, path) != 0) {
        int saved_errno = errno;
        remove(temporary);
        return failure(error, "Cannot replace CSV; original preserved: %s", strerror(saved_errno));
    }
#endif
    return true;
}
static bool create_table(const Database *db, const Query *query, FILE *output, char *error) {
    char schema[640], csv[640];
    FILE *schema_file, *csv_file;
    Table table = {0};
    struct stat info;
    bool ok = true;
    if (!path_for(db, query->table, ".schema", schema, error) || !path_for(db, query->table, ".csv", csv, error)) return false;
    if (stat(schema, &info) == 0 || stat(csv, &info) == 0) return failure(error, "Table '%s' already exists (or has leftover files)", query->table);
    if (!query->column_count || query->column_count > DB_MAX_COLUMNS) return failure(error, "Invalid column count");
    for (size_t i = 0; i < query->column_count; ++i) {
        if (!valid_name(query->columns[i].name) || column_index(&table, query->columns[i].name) >= 0)
            return failure(error, "Invalid or duplicate column '%s'", query->columns[i].name);
        table.columns[table.column_count++] = query->columns[i];
    }
    schema_file = fopen(schema, "wbx");
    if (!schema_file) return failure(error, "Cannot create schema: %s", strerror(errno));
    for (size_t i = 0; i < table.column_count; ++i) {
        if (fprintf(schema_file, "%s:%s\n", table.columns[i].name, db_type_name(table.columns[i].type)) < 0) ok = false;
    }
    if (fclose(schema_file) != 0) ok = false;
    if (!ok) { remove(schema); return failure(error, "Schema write failed"); }
    csv_file = fopen(csv, "wbx");
    if (!csv_file) { remove(schema); return failure(error, "Cannot create CSV: %s", strerror(errno)); }
    ok = write_table(csv_file, &table, error);
    if (fclose(csv_file) != 0) ok = failure(error, "CSV close failed");
    if (!ok) { remove(csv); remove(schema); return false; }
    fprintf(output, "Table '%s' created.\n", query->table);
    return true;
}
static bool coerce(Value *value, DataType type, char *error) {
    if (value->type == TYPE_INT && type == TYPE_FLOAT) {
        double number = value->as.integer;
        value->type = TYPE_FLOAT; value->as.floating = number;
    }
    return value->type == type || failure(error, "Type mismatch: expected %s, got %s", db_type_name(type), db_type_name(value->type));
}
static bool numeric(DataType type) { return type == TYPE_INT || type == TYPE_FLOAT; }
static bool validate_condition(const Table *table, const Condition *condition, int *index, char *error) {
    DataType left, right;
    *index = -1;
    if (!condition->present) return true;
    *index = column_index(table, condition->column);
    if (*index < 0) return failure(error, "Unknown WHERE column '%s'", condition->column);
    left = table->columns[*index].type; right = condition->value.type;
    if (left != right && !(numeric(left) && numeric(right))) return failure(error, "Incompatible WHERE types");
    if (left == TYPE_BOOL && condition->op != OP_EQ && condition->op != OP_NE)
        return failure(error, "BOOL supports only = and !=");
    return true;
}
static bool matches(const Row *row, const Condition *condition, int index) {
    const Value *left, *right;
    int order;
    if (index < 0) return true;
    left = &row->values[index]; right = &condition->value;
    if (numeric(left->type)) {
        double a = left->type == TYPE_INT ? left->as.integer : left->as.floating;
        double b = right->type == TYPE_INT ? right->as.integer : right->as.floating;
        order = (a > b) - (a < b);
    } else if (left->type == TYPE_STRING) order = strcmp(left->as.string, right->as.string);
    else order = (int)left->as.boolean - (int)right->as.boolean;
    switch (condition->op) {
        case OP_EQ: return order == 0; case OP_NE: return order != 0;
        case OP_LT: return order < 0; case OP_GT: return order > 0;
        case OP_LE: return order <= 0; case OP_GE: return order >= 0;
    }
    return false;
}
static void display_value(const Value *value, char *display) {
    char text[DB_MAX_STRING + 1];
    size_t n = 0;
    value_text(value, text);
    /* Escape control bytes so embedded newlines cannot break the terminal table. */
    for (size_t i = 0; text[i]; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (c == '\n' || c == '\r' || c == '\t' || c == '\\') {
            display[n++] = '\\';
            display[n++] = c == '\n' ? 'n' : c == '\r' ? 'r' : c == '\t' ? 't' : '\\';
        } else if (c < 32 || c >= 127) {
            snprintf(display + n, 5, "\\x%02X", c); n += 4;
        } else display[n++] = (char)c;
    }
    display[n] = '\0';
}
static void border(FILE *output, const size_t *widths, size_t count) {
    fputc('+', output);
    for (size_t c = 0; c < count; ++c) {
        for (size_t i = 0; i < widths[c] + 2; ++i) fputc('-', output);
        fputc('+', output);
    }
    fputc('\n', output);
}
static bool select_rows(const Table *table, const Query *query, int where_index, FILE *output, char *error) {
    int indices[DB_MAX_COLUMNS];
    size_t widths[DB_MAX_COLUMNS], count = query->select_all ? table->column_count : query->column_count, returned = 0;
    char display[4 * DB_MAX_STRING + 1];
    for (size_t c = 0; c < count; ++c) {
        indices[c] = query->select_all ? (int)c : column_index(table, query->columns[c].name);
        if (indices[c] < 0) return failure(error, "Unknown selected column '%s'", query->columns[c].name);
        widths[c] = strlen(table->columns[indices[c]].name);
    }
    for (size_t r = 0; r < table->row_count; ++r) if (matches(&table->rows[r], &query->where, where_index)) {
        ++returned;
        for (size_t c = 0; c < count; ++c) {
            display_value(&table->rows[r].values[indices[c]], display);
            if (strlen(display) > widths[c]) widths[c] = strlen(display);
        }
    }
    border(output, widths, count);
    fputc('|', output);
    for (size_t c = 0; c < count; ++c) fprintf(output, " %-*s |", (int)widths[c], table->columns[indices[c]].name);
    fputc('\n', output);
    border(output, widths, count);
    for (size_t r = 0; r < table->row_count; ++r) if (matches(&table->rows[r], &query->where, where_index)) {
        fputc('|', output);
        for (size_t c = 0; c < count; ++c) {
            display_value(&table->rows[r].values[indices[c]], display);
            fprintf(output, " %-*s |", (int)widths[c], display);
        }
        fputc('\n', output);
    }
    border(output, widths, count);
    fprintf(output, "%zu row%s returned.\n", returned, returned == 1 ? "" : "s");
    return true;
}
bool db_execute(Database *db, const Query *query, FILE *output, char *error) {
    Table table = {0};
    int where_index;
    bool ok = false;
    size_t affected = 0;
    error[0] = '\0';
    if (query->kind == QUERY_CREATE) return create_table(db, query, output, error);
    if (!load_table(db, query->table, &table, error) || !validate_condition(&table, &query->where, &where_index, error)) goto done;
    switch (query->kind) {
        case QUERY_SELECT: ok = select_rows(&table, query, where_index, output, error); break;
        case QUERY_INSERT: {
            Row row = {0};
            if (query->value_count != table.column_count) { failure(error, "Expected %zu values, got %zu", table.column_count, query->value_count); break; }
            for (size_t c = 0; c < table.column_count; ++c) {
                row.values[c] = query->values[c];
                if (!coerce(&row.values[c], table.columns[c].type, error)) goto done;
            }
            if (add_row(&table, &row, error) && save_table(db, query->table, &table, error)) {
                fprintf(output, "1 row inserted.\n"); ok = true;
            }
            break;
        }
        case QUERY_UPDATE: {
            int index = column_index(&table, query->update_column);
            Value value = query->update_value;
            if (index < 0) { failure(error, "Unknown update column '%s'", query->update_column); break; }
            if (!coerce(&value, table.columns[index].type, error)) break;
            for (size_t r = 0; r < table.row_count; ++r) if (matches(&table.rows[r], &query->where, where_index)) {
                table.rows[r].values[index] = value; ++affected;
            }
            ok = !affected || save_table(db, query->table, &table, error);
            if (ok) fprintf(output, "%zu row%s updated.\n", affected, affected == 1 ? "" : "s");
            break;
        }
        case QUERY_DELETE: {
            size_t kept = 0;
            for (size_t r = 0; r < table.row_count; ++r) {
                if (matches(&table.rows[r], &query->where, where_index)) ++affected;
                else table.rows[kept++] = table.rows[r];
            }
            table.row_count = kept;
            ok = !affected || save_table(db, query->table, &table, error);
            if (ok) fprintf(output, "%zu row%s deleted.\n", affected, affected == 1 ? "" : "s");
            break;
        }
        case QUERY_DROP: {
            char csv[640], schema[640];
            if (!path_for(db, query->table, ".csv", csv, error) || !path_for(db, query->table, ".schema", schema, error)) break;
            if (remove(csv) != 0) { failure(error, "Cannot remove CSV: %s", strerror(errno)); break; }
            if (remove(schema) != 0) { failure(error, "CSV removed but schema removal failed: %s", strerror(errno)); break; }
            fprintf(output, "Table '%s' dropped.\n", query->table); ok = true; break;
        }
        default: failure(error, "Invalid query kind"); break;
    }
done:
    free(table.rows);
    if (ferror(output)) return failure(error, "Output write failed");
    return ok;
}
