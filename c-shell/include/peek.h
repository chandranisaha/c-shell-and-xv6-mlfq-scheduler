#ifndef CSHELL_PEEK_H
#define CSHELL_PEEK_H

#include "shell.h"
#include "token.h"

int peek_execute(const ShellState *state, const TokenList *tokens);

#endif
