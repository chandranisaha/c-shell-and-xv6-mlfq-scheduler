#include "token.h"

#include <stdlib.h>
#include <string.h>

void token_list_init(TokenList *tokens)
{
    tokens->items = NULL;
    tokens->count = 0;
    tokens->capacity = 0;
}

void token_list_destroy(TokenList *tokens)
{
    if (tokens == NULL) {
        return;
    }

    for (size_t index = 0; index < tokens->count; index++) {
        free(tokens->items[index].value);
    }
    free(tokens->items);
    token_list_init(tokens);
}

static int token_list_grow(TokenList *tokens)
{
    if (tokens->count < tokens->capacity) {
        return 0;
    }

    size_t new_capacity = tokens->capacity == 0 ? 16 : tokens->capacity * 2;
    Token *expanded = realloc(tokens->items, new_capacity * sizeof(*expanded));
    if (expanded == NULL) {
        return -1;
    }

    tokens->items = expanded;
    tokens->capacity = new_capacity;
    return 0;
}

int token_list_append_operator(TokenList *tokens, TokenType type)
{
    if (token_list_grow(tokens) != 0) {
        return -1;
    }

    tokens->items[tokens->count] = (Token){.type = type, .value = NULL};
    tokens->count++;
    return 0;
}

int token_list_append_word(TokenList *tokens, const char *value, size_t length)
{
    char *copy = malloc(length + 1);
    if (copy == NULL) {
        return -1;
    }
    if (length > 0) {
        memcpy(copy, value, length);
    }
    copy[length] = '\0';

    if (token_list_grow(tokens) != 0) {
        free(copy);
        return -1;
    }

    tokens->items[tokens->count] = (Token){.type = TOKEN_WORD, .value = copy};
    tokens->count++;
    return 0;
}
