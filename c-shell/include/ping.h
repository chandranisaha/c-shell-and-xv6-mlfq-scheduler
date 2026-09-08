#ifndef CSHELL_PING_H
#define CSHELL_PING_H

#include "shell.h"
#include "token.h"

/* Syntax: ping <target> <signal_number> */
int ping_execute(ShellState *state, const TokenList *tokens);

#endif
