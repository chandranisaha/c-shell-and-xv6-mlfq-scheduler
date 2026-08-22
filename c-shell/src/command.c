#include "command.h"

void command_line_init(CommandLine *command_line)
{
    token_list_init(&command_line->tokens);
}

void command_line_destroy(CommandLine *command_line)
{
    token_list_destroy(&command_line->tokens);
}
