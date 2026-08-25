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

int exec_parse_pipeline(const TokenList *tokens, Pipeline *pipeline)
{
    if (tokens == NULL || pipeline == NULL) {
        return -1;
    }

    *pipeline = (Pipeline){0};
    size_t position = 0;
    while (position < tokens->count) {
        if (tokens->items[position].type != TOKEN_WORD ||
            pipeline->count >= MAX_PIPELINE) {
            return -1;
        }

        FlatCmd *command = &pipeline->commands[pipeline->count];
        while (position < tokens->count) {
            TokenType type = tokens->items[position].type;
            if (type == TOKEN_WORD) {
                if (command->argc >= MAX_ARGS) {
                    return -1;
                }
                command->argv[command->argc++] =
                    tokens->items[position].value;
                position++;
                continue;
            }
            if (type == TOKEN_LT || type == TOKEN_GT ||
                type == TOKEN_GTGT) {
                if (command->redir_count >= MAX_REDIRS ||
                    position + 1 >= tokens->count ||
                    tokens->items[position + 1].type != TOKEN_WORD) {
                    return -1;
                }
                command->redirs[command->redir_count++] =
                    (RedirSpec){.filename =
                                    tokens->items[position + 1].value,
                                .type = type};
                position += 2;
                continue;
            }
            break;
        }
        command->argv[command->argc] = NULL;
        if (command->argc == 0) {
            return -1;
        }
        pipeline->count++;

        if (position >= tokens->count ||
            tokens->items[position].type == TOKEN_SEMI ||
            tokens->items[position].type == TOKEN_AMP) {
            break;
        }
        if (tokens->items[position].type != TOKEN_PIPE) {
            return -1;
        }
        position++;
    }
    return pipeline->count == 0 ? -1 : 0;
}
