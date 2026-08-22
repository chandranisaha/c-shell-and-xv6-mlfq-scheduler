#ifndef CSHELL_COMMAND_H
#define CSHELL_COMMAND_H

#include "token.h"

typedef struct {
    TokenList tokens;
} CommandLine;

void command_line_init(CommandLine *command_line);
void command_line_destroy(CommandLine *command_line);

#endif
