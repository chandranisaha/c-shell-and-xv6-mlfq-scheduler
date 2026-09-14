#ifndef CSHELL_RESUME_H
#define CSHELL_RESUME_H

#include "shell.h"
#include "token.h"

int resume_execute(ShellState *state, const TokenList *tokens);

#endif
