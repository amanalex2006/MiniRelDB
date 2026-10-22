#include "parser.h"

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct { Lexer lexer; Token token; char *error; } Parser;
static bool next(Parser *p) { return lexer_next(&p->lexer, &p->token, p->error); }
static bool syntax(Parser *p, const char *message) {
    snprintf(p->error, DB_ERROR_SIZE, "Syntax error at byte %zu: %s", p->token.position + 1, message);
    return false;
}
static bool expect(Parser *p, TokenKind kind, const char *message) {
    if (p->token.kind != kind) return syntax(p, message);
    return next(p);
}
static bool name(Parser *p, char *destination) {
    if (p->token.kind != TOK_IDENTIFIER) return syntax(p, "expected an identifier (reserved words are not names)");
    strcpy(destination, p->token.text);
    return next(p);
}
static bool literal(Parser *p, Value *value) {
    char *end;
    errno = 0;
    switch (p->token.kind) {
        case TOK_INTEGER: {
            long long number = strtoll(p->token.text, &end, 10);
            if (errno || *end || number < INT32_MIN || number > INT32_MAX)
                return syntax(p, "INT literal outside signed 32-bit range");
            value->type = TYPE_INT; value->as.integer = (int32_t)number; break;
        }
        case TOK_FLOAT:
            value->type = TYPE_FLOAT;
            value->as.floating = strtod(p->token.text, &end);
            if (errno || *end || !isfinite(value->as.floating)) return syntax(p, "FLOAT literal out of range");
            break;
        case TOK_STRING: value->type = TYPE_STRING; strcpy(value->as.string, p->token.text); break;
        case TOK_TRUE: case TOK_FALSE:
            value->type = TYPE_BOOL; value->as.boolean = p->token.kind == TOK_TRUE; break;
        default: return syntax(p, "expected INT, FLOAT, single-quoted STRING, or BOOL literal");
    }
    return next(p);
}
static bool condition(Parser *p, Condition *where) {
    if (p->token.kind != TOK_WHERE) return true;
    where->present = true;
    if (!next(p) || !name(p, where->column)) return false;
    switch (p->token.kind) {
        case TOK_EQ: where->op = OP_EQ; break;
        case TOK_NE: where->op = OP_NE; break;
        case TOK_LT: where->op = OP_LT; break;
        case TOK_GT: where->op = OP_GT; break;
        case TOK_LE: where->op = OP_LE; break;
        case TOK_GE: where->op = OP_GE; break;
        default: return syntax(p, "expected =, !=, <, >, <=, or >=");
    }
    return next(p) && literal(p, &where->value);
}

bool parse_query(const char *sql, Query *query, char *error) {
    Parser p;
    if (strlen(sql) > DB_MAX_QUERY) { snprintf(error, DB_ERROR_SIZE, "Query exceeds 64 KiB"); return false; }
    memset(query, 0, sizeof *query);
    error[0] = '\0';
    lexer_init(&p.lexer, sql); p.error = error;
    if (!next(&p)) return false;
    switch (p.token.kind) {
        case TOK_CREATE:
            query->kind = QUERY_CREATE;
            if (!next(&p) || !expect(&p, TOK_TABLE, "expected TABLE") || !name(&p, query->table) ||
                !expect(&p, TOK_LPAREN, "expected (")) return false;
            for (;;) {
                Column *column;
                if (query->column_count == DB_MAX_COLUMNS) return syntax(&p, "maximum 32 columns");
                column = &query->columns[query->column_count++];
                if (!name(&p, column->name)) return false;
                switch (p.token.kind) {
                    case TOK_INT_TYPE: column->type = TYPE_INT; break;
                    case TOK_FLOAT_TYPE: column->type = TYPE_FLOAT; break;
                    case TOK_STRING_TYPE: column->type = TYPE_STRING; break;
                    case TOK_BOOL_TYPE: column->type = TYPE_BOOL; break;
                    default: return syntax(&p, "expected INT, FLOAT, STRING, or BOOL");
                }
                if (!next(&p)) return false;
                if (p.token.kind != TOK_COMMA) break;
                if (!next(&p)) return false;
            }
            if (!expect(&p, TOK_RPAREN, "expected )")) return false;
            break;
        case TOK_INSERT:
            query->kind = QUERY_INSERT;
            if (!next(&p) || !expect(&p, TOK_INTO, "expected INTO") || !name(&p, query->table) ||
                !expect(&p, TOK_VALUES, "expected VALUES") || !expect(&p, TOK_LPAREN, "expected (")) return false;
            for (;;) {
                if (query->value_count == DB_MAX_COLUMNS) return syntax(&p, "maximum 32 values");
                if (!literal(&p, &query->values[query->value_count++])) return false;
                if (p.token.kind != TOK_COMMA) break;
                if (!next(&p)) return false;
            }
            if (!expect(&p, TOK_RPAREN, "expected )")) return false;
            break;
        case TOK_SELECT:
            query->kind = QUERY_SELECT;
            if (!next(&p)) return false;
            if (p.token.kind == TOK_STAR) { query->select_all = true; if (!next(&p)) return false; }
            else for (;;) {
                if (query->column_count == DB_MAX_COLUMNS) return syntax(&p, "maximum 32 selected columns");
                if (!name(&p, query->columns[query->column_count++].name)) return false;
                if (p.token.kind != TOK_COMMA) break;
                if (!next(&p)) return false;
            }
            if (!expect(&p, TOK_FROM, "expected FROM") || !name(&p, query->table) || !condition(&p, &query->where)) return false;
            break;
        case TOK_UPDATE:
            query->kind = QUERY_UPDATE;
            if (!next(&p) || !name(&p, query->table) || !expect(&p, TOK_SET, "expected SET") ||
                !name(&p, query->update_column) || !expect(&p, TOK_EQ, "expected =") ||
                !literal(&p, &query->update_value) || !condition(&p, &query->where)) return false;
            break;
        case TOK_DELETE:
            query->kind = QUERY_DELETE;
            if (!next(&p) || !expect(&p, TOK_FROM, "expected FROM") || !name(&p, query->table) || !condition(&p, &query->where)) return false;
            break;
        case TOK_DROP:
            query->kind = QUERY_DROP;
            if (!next(&p) || !expect(&p, TOK_TABLE, "expected TABLE") || !name(&p, query->table)) return false;
            break;
        default: return syntax(&p, "expected CREATE, INSERT, SELECT, UPDATE, DELETE, or DROP");
    }
    return expect(&p, TOK_SEMICOLON, "expected terminating semicolon") &&
           (p.token.kind == TOK_END || syntax(&p, "unexpected text after semicolon"));
}
