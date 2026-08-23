#include "builtin.h"

#include "hop.h"

#include <string.h>

BuiltinResult builtin_execute(ShellState *state,
                              const CommandLine *command_line)
{
    if (state == NULL || command_line == NULL ||
        command_line->tokens.count == 0 ||
        command_line->tokens.items[0].type != TOKEN_WORD) {
        return BUILTIN_NOT_FOUND;
    }

    const char *name = command_line->tokens.items[0].value;
    if (strcmp(name, "hop") == 0) {
        return hop_execute(state, &command_line->tokens) == 0
                   ? BUILTIN_HANDLED
                   : BUILTIN_ERROR;
    }
    if (strcmp(name, "reveal") == 0) {
        return reveal_execute(state, &command_line->tokens) == 0
                   ? BUILTIN_HANDLED
                   : BUILTIN_ERROR;
    }
    if (strcmp(name, "peek") == 0) {
        return peek_execute(state, &command_line->tokens) == 0
                   ? BUILTIN_HANDLED
                   : BUILTIN_ERROR;
    }
    if (strcmp(name, "locate") == 0) {
        return locate_execute(state, &command_line->tokens) == 0
                   ? BUILTIN_HANDLED
                   : BUILTIN_ERROR;
    }

    return BUILTIN_NOT_FOUND;
}
