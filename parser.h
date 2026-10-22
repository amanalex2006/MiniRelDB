#ifndef MINIRELDB_PARSER_H
#define MINIRELDB_PARSER_H

#include "lexer.h"

typedef enum { QUERY_CREATE, QUERY_INSERT, QUERY_SELECT, QUERY_UPDATE, QUERY_DELETE, QUERY_DROP } QueryKind;
struct Query {
    QueryKind kind;
    char table[DB_MAX_NAME + 1];
    Column columns[DB_MAX_COLUMNS];
    size_t column_count;
    Value values[DB_MAX_COLUMNS];
    size_t value_count;
    bool select_all;
    char update_column[DB_MAX_NAME + 1];
    Value update_value;
    Condition where;
};

bool parse_query(const char *sql, Query *query, char *error);

#endif
