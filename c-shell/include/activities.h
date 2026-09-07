#ifndef CSHELL_ACTIVITIES_H
#define CSHELL_ACTIVITIES_H

#include "shell.h"
#include "token.h"

/* Prints one line per tracked process group followed by an indented line
 * per still-running process in it, oldest group first. Reaps finished
 * background processes first so exited processes never appear. */
int activities_execute(ShellState *state, const TokenList *tokens);

#endif
