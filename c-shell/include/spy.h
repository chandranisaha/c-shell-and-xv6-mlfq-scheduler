#ifndef CSHELL_SPY_H
#define CSHELL_SPY_H

#include "shell.h"
#include "token.h"

/* F1: lists the open files of a process the way lsof does - working
 * directory, executable text, unique memory mappings and numeric file
 * descriptors. With no argument it reports the shell itself. */
int spy_execute(const ShellState *state, const TokenList *tokens);

#endif
