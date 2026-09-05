#include "exec.h"

#include <stddef.h>

/*
 * D1 owns only semicolon sequencing. Ampersand remains a Part C boundary
 * until D2 adds background-job launch and tracking.
 */
ExecResult execute_part_d(const CommandLine *command_line, ShellState *state)
{
    if (command_line == NULL || state == NULL) {
        return EXEC_ERROR;
    }

    const TokenList *tokens = &command_line->tokens;
    size_t start = 0;
    size_t position = 0;

    while (position <= tokens->count) {
        if (position < tokens->count &&
            tokens->items[position].type != TOKEN_SEMI &&
            tokens->items[position].type != TOKEN_AMP) {
            position++;
            continue;
        }

        if (position == start) {
            return EXEC_ERROR;
        }

        CommandLine segment = {
            .tokens = {
                .items = &tokens->items[start],
                .count = position - start,
                .capacity = position - start,
            },
        };
        ExecResult result = execute_part_c(&segment, state);
        if (result != EXEC_HANDLED) {
            return result;
        }

        if (position == tokens->count ||
            tokens->items[position].type == TOKEN_AMP) {
            return EXEC_HANDLED;
        }

        start = position + 1;
        position = start;
    }

    return EXEC_HANDLED;
}
