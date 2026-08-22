#ifndef CSHELL_LEXER_H
#define CSHELL_LEXER_H

#include "token.h"

typedef enum {
    LEXER_SUCCESS,
    LEXER_INVALID_SYNTAX,
    LEXER_MEMORY_ERROR
} LexerResult;

LexerResult lexer_tokenize(const char *line, TokenList *tokens);

#endif
