#ifndef MINIRELDB_LEXER_H
#define MINIRELDB_LEXER_H

#include "database.h"

typedef enum {
    TOK_END, TOK_IDENTIFIER, TOK_INTEGER, TOK_FLOAT, TOK_STRING,
    TOK_TRUE, TOK_FALSE, TOK_CREATE, TOK_TABLE, TOK_INSERT, TOK_INTO,
    TOK_VALUES, TOK_SELECT, TOK_FROM, TOK_WHERE, TOK_UPDATE, TOK_SET,
    TOK_DELETE, TOK_DROP, TOK_INT_TYPE, TOK_FLOAT_TYPE, TOK_STRING_TYPE,
    TOK_BOOL_TYPE, TOK_LPAREN, TOK_RPAREN, TOK_COMMA, TOK_SEMICOLON,
    TOK_STAR, TOK_EQ, TOK_NE, TOK_LT, TOK_GT, TOK_LE, TOK_GE
} TokenKind;
typedef struct { TokenKind kind; char text[DB_MAX_STRING + 1]; size_t position; } Token;
typedef struct { const char *input; size_t position; } Lexer;

void lexer_init(Lexer *lexer, const char *input);
bool lexer_next(Lexer *lexer, Token *token, char *error);

#endif
