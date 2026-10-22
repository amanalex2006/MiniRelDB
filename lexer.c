#include "lexer.h"

#include <ctype.h>
#include <string.h>

static bool alpha(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
static bool digit(unsigned char c) { return c >= '0' && c <= '9'; }
static bool fail(char *error, size_t pos, const char *message) {
    snprintf(error, DB_ERROR_SIZE, "Lexical error at byte %zu: %s", pos + 1, message);
    return false;
}

void lexer_init(Lexer *lexer, const char *input) {
    lexer->input = input;
    lexer->position = 0;
}

bool lexer_next(Lexer *lexer, Token *token, char *error) {
    static const struct { const char *word; TokenKind kind; } keywords[] = {
        {"create", TOK_CREATE}, {"table", TOK_TABLE}, {"insert", TOK_INSERT},
        {"into", TOK_INTO}, {"values", TOK_VALUES}, {"select", TOK_SELECT},
        {"from", TOK_FROM}, {"where", TOK_WHERE}, {"update", TOK_UPDATE},
        {"set", TOK_SET}, {"delete", TOK_DELETE}, {"drop", TOK_DROP},
        {"int", TOK_INT_TYPE}, {"float", TOK_FLOAT_TYPE},
        {"string", TOK_STRING_TYPE}, {"bool", TOK_BOOL_TYPE},
        {"true", TOK_TRUE}, {"false", TOK_FALSE}
    };
    const char *s = lexer->input;
    size_t p = lexer->position, start, n;
    while (isspace((unsigned char)s[p])) ++p;
    token->position = p;
    token->text[0] = '\0';
    start = p;
    if (!s[p]) { token->kind = TOK_END; lexer->position = p; return true; }
    if (alpha((unsigned char)s[p])) {
        while (alpha((unsigned char)s[p]) || digit((unsigned char)s[p])) ++p;
        n = p - start;
        if (n > DB_MAX_NAME) return fail(error, start, "identifier exceeds 63 bytes");
        for (size_t i = 0; i < n; ++i) token->text[i] = (char)tolower((unsigned char)s[start + i]);
        token->text[n] = '\0';
        token->kind = TOK_IDENTIFIER;
        for (size_t i = 0; i < sizeof keywords / sizeof keywords[0]; ++i) {
            if (!strcmp(token->text, keywords[i].word)) { token->kind = keywords[i].kind; break; }
        }
    } else if (s[p] == '\'') {
        ++p;
        n = 0;
        for (;;) {
            if (!s[p]) return fail(error, start, "unterminated string literal");
            if (s[p] == '\'') {
                if (s[p + 1] != '\'') { ++p; break; }
                ++p; /* SQL escapes an apostrophe by doubling it. */
            }
            if (n == DB_MAX_STRING) return fail(error, start, "string exceeds 255 bytes");
            token->text[n++] = s[p++];
        }
        token->text[n] = '\0';
        token->kind = TOK_STRING;
    } else if (digit((unsigned char)s[p]) || s[p] == '+' || s[p] == '-' || s[p] == '.') {
        bool floating = false, has_digits = false;
        if (s[p] == '+' || s[p] == '-') ++p;
        while (digit((unsigned char)s[p])) { ++p; has_digits = true; }
        if (s[p] == '.') {
            floating = true;
            ++p;
            while (digit((unsigned char)s[p])) { ++p; has_digits = true; }
        }
        if (!has_digits) return fail(error, start, "expected a numeric literal");
        if (s[p] == 'e' || s[p] == 'E') {
            floating = true;
            ++p;
            if (s[p] == '+' || s[p] == '-') ++p;
            if (!digit((unsigned char)s[p])) return fail(error, p, "exponent requires digits");
            while (digit((unsigned char)s[p])) ++p;
        }
        if (alpha((unsigned char)s[p]) || s[p] == '.') return fail(error, p, "invalid numeric literal");
        n = p - start;
        if (n > DB_MAX_STRING) return fail(error, start, "numeric literal too long");
        memcpy(token->text, s + start, n);
        token->text[n] = '\0';
        token->kind = floating ? TOK_FLOAT : TOK_INTEGER;
    } else {
        switch (s[p++]) {
            case '(': token->kind = TOK_LPAREN; break;
            case ')': token->kind = TOK_RPAREN; break;
            case ',': token->kind = TOK_COMMA; break;
            case ';': token->kind = TOK_SEMICOLON; break;
            case '*': token->kind = TOK_STAR; break;
            case '=': token->kind = TOK_EQ; break;
            case '!':
                if (s[p] != '=') return fail(error, start, "expected !=");
                ++p; token->kind = TOK_NE; break;
            case '<': token->kind = s[p] == '=' ? (++p, TOK_LE) : TOK_LT; break;
            case '>': token->kind = s[p] == '=' ? (++p, TOK_GE) : TOK_GT; break;
            default: return fail(error, start, "unexpected character");
        }
    }
    lexer->position = p;
    return true;
}
