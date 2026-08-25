#include "exec.h"

#include <stddef.h>

int exec_parse_first(const TokenList *tokens, FlatCmd *command)
{
    if (tokens == NULL || command == NULL) {
        return -1;
    }

    *command = (FlatCmd){0};
    for (size_t index = 0; index < tokens->count; index++) {
        const Token *token = &tokens->items[index];
        if (token->type == TOKEN_SEMI || token->type == TOKEN_AMP) {
            break;
        }
        if (token->type == TOKEN_PIPE) {
            command->has_pipeline = 1;
            return 1;
        }
        if (token->type == TOKEN_WORD) {
            if (command->argc >= MAX_ARGS) {
                return -1;
            }
            command->argv[command->argc++] = token->value;
            continue;
        }
        if (token->type == TOKEN_LT || token->type == TOKEN_GT ||
            token->type == TOKEN_GTGT) {
            if (command->redir_count >= MAX_REDIRS || index + 1 >= tokens->count ||
                tokens->items[index + 1].type != TOKEN_WORD) {
                return -1;
            }
            command->redirs[command->redir_count++] =
                (RedirSpec){.filename = tokens->items[index + 1].value,
                            .type = token->type};
            index++;
            continue;
        }
        return -1;
    }

    command->argv[command->argc] = NULL;
    return 0;
}
