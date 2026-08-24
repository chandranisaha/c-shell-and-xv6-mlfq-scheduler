#ifndef CSHELL_LOCATE_H
#define CSHELL_LOCATE_H

#include "shell.h"
#include "token.h"

int locate_execute(const ShellState *state, const TokenList *tokens);

#endif
