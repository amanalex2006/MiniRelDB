#ifndef MINIRELDB_DATABASE_H
#define MINIRELDB_DATABASE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define DB_MAX_COLUMNS 32
#define DB_MAX_NAME 63
#define DB_MAX_STRING 255
#define DB_MAX_QUERY 65536
#define DB_MAX_ROWS 10000
#define DB_ERROR_SIZE 256

typedef enum { TYPE_INT, TYPE_FLOAT, TYPE_STRING, TYPE_BOOL } DataType;
typedef struct {
    DataType type;
    union { int32_t integer; double floating; char string[DB_MAX_STRING + 1]; bool boolean; } as;
} Value;
typedef struct { char name[DB_MAX_NAME + 1]; DataType type; } Column;
typedef enum { OP_EQ, OP_NE, OP_LT, OP_GT, OP_LE, OP_GE } CompareOp;
typedef struct {
    bool present;
    char column[DB_MAX_NAME + 1];
    CompareOp op;
    Value value;
} Condition;
typedef struct { char directory[512]; } Database;
typedef struct Query Query;

bool db_init(Database *db, const char *directory, char *error);
bool db_execute(Database *db, const Query *query, FILE *output, char *error);
const char *db_type_name(DataType type);

#endif
