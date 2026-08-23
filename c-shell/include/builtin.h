#ifndef CSHELL_BUILTIN_H
#define CSHELL_BUILTIN_H

#include "command.h"
#include "peek.h"
#include "reveal.h"
#include "locate.h"
#include "shell.h"

typedef enum {
    BUILTIN_NOT_FOUND,
    BUILTIN_HANDLED,
    BUILTIN_ERROR
} BuiltinResult;

BuiltinResult builtin_execute(ShellState *state,
                              const CommandLine *command_line);

#endif
