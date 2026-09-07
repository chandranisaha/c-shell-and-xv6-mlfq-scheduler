#include "command.h"
#include "exec.h"
#include "input.h"
#include "lexer.h"
#include "parser.h"
#include "prompt.h"
#include "shell.h"
#include "signals.h"

#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    ShellState state;
    if (shell_state_init(&state) != 0) {
        printf("cshell: failed to initialize shell state\n");
        return 1;
    }
    signals_install();
    signals_ignore_terminal();

    for (;;) {
        jobs_reap_background(&state, 0);
        signals_clear();
        if (prompt_print(&state) != 0) {
            printf("cshell: failed to print prompt\n");
            return 1;
        }

        char *line = NULL;
        InputResult result = input_read_line(&line, &state);
        if (result == INPUT_EOF) {
            putchar('\n');
            shell_state_destroy(&state);
            return 0;
        }
        if (result == INPUT_ERROR) {
            printf("cshell: failed to read input\n");
            shell_state_destroy(&state);
            return 1;
        }
        if (result == INPUT_INTERRUPTED) {
            continue;
        }
        if (result == INPUT_TOO_LONG) {
            printf("cshell: invalid syntax\n");
            continue;
        }

        CommandLine command_line;
        command_line_init(&command_line);
        LexerResult lex_result = lexer_tokenize(line, &command_line.tokens);
        free(line);

        if (lex_result == LEXER_MEMORY_ERROR) {
            command_line_destroy(&command_line);
            printf("cshell: memory allocation failed\n");
            shell_state_destroy(&state);
            return 1;
        }
        if (lex_result == LEXER_INVALID_SYNTAX ||
            !parser_validate(&command_line)) {
            printf("cshell: invalid syntax\n");
        } else {
            (void)execute_part_d(&command_line, &state);
        }

        command_line_destroy(&command_line);
    }
}
