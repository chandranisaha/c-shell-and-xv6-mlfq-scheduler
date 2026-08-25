#include "lexer.h"

#include <ctype.h>
#include <stddef.h>
#include <stdlib.h>

static int is_space(char character)
{
    return character == ' ' || character == '\t' ||
           character == '\n' || character == '\r';
}

static int is_special(char character)
{
    return character == '|' || character == '&' || character == ';' ||
           character == '<' || character == '>';
}

static int is_unprintable_nonspace(char character)
{
    return !isprint((unsigned char)character) && !is_space(character);
}

static int append_character(char **buffer, size_t *length, size_t *capacity,
                            char character)
{
    if (*length + 1 >= *capacity) {
        size_t new_capacity = *capacity == 0 ? 32 : *capacity * 2;
        char *expanded = realloc(*buffer, new_capacity);
        if (expanded == NULL) {
            return -1;
        }
        *buffer = expanded;
        *capacity = new_capacity;
    }

    (*buffer)[*length] = character;
    (*length)++;
    return 0;
}

static LexerResult scan_word(const char *line, size_t *position,
                             TokenList *tokens)
{
    char *value = NULL;
    size_t length = 0;
    size_t capacity = 0;
    int word_started = 0;

    while (line[*position] != '\0') {
        char character = line[*position];

        if (is_space(character) || is_special(character)) {
            break;
        }

        if (is_unprintable_nonspace(character)) {
            break;
        }

        word_started = 1;
        if (character == '\\') {
            (*position)++;
            if (line[*position] == '\0' ||
                append_character(&value, &length, &capacity,
                                 line[*position]) != 0) {
                free(value);
                return line[*position] == '\0' ? LEXER_INVALID_SYNTAX
                                                : LEXER_MEMORY_ERROR;
            }
            (*position)++;
            continue;
        }

        if (character == '\'') {
            (*position)++;
            while (line[*position] != '\0' && line[*position] != '\'') {
                if (is_unprintable_nonspace(line[*position])) {
                    free(value);
                    return LEXER_INVALID_SYNTAX;
                }
                if (append_character(&value, &length, &capacity,
                                     line[*position]) != 0) {
                    free(value);
                    return LEXER_MEMORY_ERROR;
                }
                (*position)++;
            }
            if (line[*position] != '\'') {
                free(value);
                return LEXER_INVALID_SYNTAX;
            }
            (*position)++;
            continue;
        }

        if (character == '"') {
            (*position)++;
            while (line[*position] != '\0' && line[*position] != '"') {
                if (is_unprintable_nonspace(line[*position]) &&
                    line[*position] != '\\') {
                    free(value);
                    return LEXER_INVALID_SYNTAX;
                }
                if (line[*position] == '\\') {
                    (*position)++;
                    if (line[*position] == '\0') {
                        free(value);
                        return LEXER_INVALID_SYNTAX;
                    }
                    if (line[*position] != '"' && line[*position] != '\\' &&
                        append_character(&value, &length, &capacity, '\\') != 0) {
                        free(value);
                        return LEXER_MEMORY_ERROR;
                    }
                }
                if (append_character(&value, &length, &capacity,
                                     line[*position]) != 0) {
                    free(value);
                    return LEXER_MEMORY_ERROR;
                }
                (*position)++;
            }
            if (line[*position] != '"') {
                free(value);
                return LEXER_INVALID_SYNTAX;
            }
            (*position)++;
            continue;
        }

        if (append_character(&value, &length, &capacity, character) != 0) {
            free(value);
            return LEXER_MEMORY_ERROR;
        }
        (*position)++;
    }

    if (!word_started || token_list_append_word(tokens, value, length) != 0) {
        free(value);
        return word_started ? LEXER_MEMORY_ERROR : LEXER_INVALID_SYNTAX;
    }
    free(value);
    return LEXER_SUCCESS;
}

LexerResult lexer_tokenize(const char *line, TokenList *tokens)
{
    token_list_init(tokens);
    size_t position = 0;

    while (line[position] != '\0') {
        char character = line[position];
        if (is_space(character)) {
            position++;
            continue;
        }
        if (is_unprintable_nonspace(character)) {
            position++;
            continue;
        }

        TokenType type;
        if (character == '|') {
            type = TOKEN_PIPE;
            position++;
        } else if (character == '&') {
            type = TOKEN_AMP;
            position++;
        } else if (character == ';') {
            type = TOKEN_SEMI;
            position++;
        } else if (character == '<') {
            type = TOKEN_LT;
            position++;
        } else if (character == '>') {
            if (line[position + 1] == '>') {
                type = TOKEN_GTGT;
                position += 2;
            } else {
                type = TOKEN_GT;
                position++;
            }
        } else {
            LexerResult result = scan_word(line, &position, tokens);
            if (result != LEXER_SUCCESS) {
                token_list_destroy(tokens);
                return result;
            }
            continue;
        }

        if (token_list_append_operator(tokens, type) != 0) {
            token_list_destroy(tokens);
            return LEXER_MEMORY_ERROR;
        }
    }

    return LEXER_SUCCESS;
}
