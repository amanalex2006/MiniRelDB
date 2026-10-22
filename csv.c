#include "csv.h"

#include <string.h>

static int csv_error(char *error, const char *message) {
    snprintf(error, DB_ERROR_SIZE, "CSV error: %s", message);
    return -1;
}

int csv_read_record(FILE *file, CsvRecord *record, char *error) {
    enum { START, PLAIN, QUOTED, CLOSED } state = START;
    size_t length = 0;
    bool seen = false;
    record->count = 0;
    for (;;) {
        int c = fgetc(file);
        bool delimiter = false, end_record = false;
        if (c == EOF) {
            if (ferror(file)) return csv_error(error, "read failed");
            if (state == QUOTED) return csv_error(error, "unterminated quoted field");
            if (!seen) return 0;
            delimiter = end_record = true;
        } else {
            seen = true;
            if (c == 0) return csv_error(error, "NUL bytes are not supported");
            if (state == QUOTED) {
                if (c == '"') { state = CLOSED; continue; }
            } else if (state == CLOSED && c == '"') { state = QUOTED; }
            else if (c == ',' || c == '\n' || c == '\r') {
                delimiter = true;
                end_record = c != ',';
                if (c == '\r') {
                    int following = fgetc(file);
                    if (following != '\n' && following != EOF) ungetc(following, file);
                    if (ferror(file)) return csv_error(error, "read failed");
                }
            } else if (state == CLOSED) return csv_error(error, "unexpected character after closing quote");
            else if (state == START && c == '"') { state = QUOTED; continue; }
            else if (c == '"') return csv_error(error, "quote inside unquoted field");
            else state = PLAIN;
        }
        if (delimiter) {
            if (record->count == DB_MAX_COLUMNS) return csv_error(error, "too many fields");
            record->fields[record->count++][length] = '\0';
            if (end_record) return 1;
            length = 0; state = START;
        } else {
            if (record->count == DB_MAX_COLUMNS) return csv_error(error, "too many fields");
            if (length == DB_MAX_STRING) return csv_error(error, "field exceeds 255 bytes");
            record->fields[record->count][length++] = (char)c;
        }
    }
}

bool csv_write_record(FILE *file, const CsvRecord *record, char *error) {
    if (!record->count || record->count > DB_MAX_COLUMNS) { csv_error(error, "invalid field count"); return false; }
    for (size_t i = 0; i < record->count; ++i) {
        const char *s = record->fields[i];
        bool quote = strpbrk(s, ",\"\r\n") != NULL;
        if (i && fputc(',', file) == EOF) goto failure;
        if (quote && fputc('"', file) == EOF) goto failure;
        for (; *s; ++s) {
            if (*s == '"' && fputc('"', file) == EOF) goto failure;
            if (fputc((unsigned char)*s, file) == EOF) goto failure;
        }
        if (quote && fputc('"', file) == EOF) goto failure;
    }
    if (fputc('\n', file) != EOF) return true;
failure:
    csv_error(error, "write failed");
    return false;
}
