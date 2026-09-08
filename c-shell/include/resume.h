#ifndef CSHELL_RESUME_H
#define CSHELL_RESUME_H

#include "shell.h"
#include "token.h"

/* Syntax: resume %job_number (fg [--timeout <seconds>] | bg) */
int resume_execute(ShellState *state, const TokenList *tokens);

#endif
