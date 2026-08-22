#ifndef CSHELL_TOKEN_H
#define CSHELL_TOKEN_H

#include <stddef.h>

typedef enum {
    TOKEN_WORD,
    TOKEN_PIPE,
    TOKEN_AMP,
    TOKEN_SEMI,
    TOKEN_LT,
    TOKEN_GT,
    TOKEN_GTGT
} TokenType;

typedef struct {
    TokenType type;
    char *value;
} Token;

typedef struct {
    Token *items;
    size_t count;
    size_t capacity;
} TokenList;

void token_list_init(TokenList *tokens);
void token_list_destroy(TokenList *tokens);
int token_list_append_operator(TokenList *tokens, TokenType type);
int token_list_append_word(TokenList *tokens, const char *value, size_t length);

#endif
