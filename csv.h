#ifndef MINIRELDB_CSV_H
#define MINIRELDB_CSV_H

#include "database.h"

typedef struct { size_t count; char fields[DB_MAX_COLUMNS][DB_MAX_STRING + 1]; } CsvRecord;
/* Returns 1 for a record, 0 at clean EOF, and -1 for an error. */
int csv_read_record(FILE *file, CsvRecord *record, char *error);
bool csv_write_record(FILE *file, const CsvRecord *record, char *error);

#endif
