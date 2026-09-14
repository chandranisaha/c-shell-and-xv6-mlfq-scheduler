#ifndef CSHELL_SNOOP_H
#define CSHELL_SNOOP_H

#include "shell.h"
#include "token.h"

int snoop_execute(const ShellState *state, const TokenList *tokens);

#endif
