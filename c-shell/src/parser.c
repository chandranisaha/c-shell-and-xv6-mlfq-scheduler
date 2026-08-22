#include "parser.h"

static int parse_argument(const TokenList *tokens, size_t *position);
/* 
 * Validates a command (CMD -> WORD ARG).
 * Expects an executable command name as the leading TOKEN_WORD (e.g., after '|' or ';').
 */
static int parse_command(const TokenList *tokens, size_t *position)
{
    if (*position >= tokens->count ||
        tokens->items[*position].type != TOKEN_WORD) {
        return 0;
    }
    (*position)++;
    return parse_argument(tokens, position);
}
/* 
 * Validates a redirection target (TGT -> WORD ARG).
 * Grammatically identical to CMD, but contextually identifies a target file 
 * following a redirection operator ('<', '>', '>>') rather than an executable.
 */
static int parse_target(const TokenList *tokens, size_t *position)
{
    return parse_command(tokens, position);
}

/* 
 * Validates a background command (BKG -> CMD).
 * Grammatically identical to CMD, but contextually identifies a command 
 * that should run in the background.
 */
static int parse_background(const TokenList *tokens, size_t *position)
{
    if (*position == tokens->count) {
        return 1;
    }
    return parse_command(tokens, position);
}

static int parse_argument(const TokenList *tokens, size_t *position)
{
    if (*position == tokens->count) {
        return 1;
    }

    TokenType type = tokens->items[*position].type;
    if (type == TOKEN_WORD) {
        (*position)++;
        return parse_argument(tokens, position);
    }

    if (type == TOKEN_LT || type == TOKEN_GT || type == TOKEN_GTGT) {
        (*position)++;
        return parse_target(tokens, position);
    }

    if (type == TOKEN_PIPE || type == TOKEN_SEMI) {
        (*position)++;
        return parse_command(tokens, position);
    }

    if (type == TOKEN_AMP) {
        (*position)++;
        return parse_background(tokens, position);
    }

    return 0;
}

int parser_validate(const CommandLine *command_line)
{
    size_t position = 0;
    const TokenList *tokens = &command_line->tokens;

    if (tokens->count == 0) {
        return 1;
    }

    if (!parse_command(tokens, &position)) {
        return 0;
    }
    return position == tokens->count;
}
