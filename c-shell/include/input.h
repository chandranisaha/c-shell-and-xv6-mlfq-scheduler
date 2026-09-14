#ifndef CSHELL_INPUT_H
#define CSHELL_INPUT_H

typedef enum {
    INPUT_LINE,
    INPUT_EOF,
    INPUT_ERROR,
    INPUT_TOO_LONG,
    INPUT_INTERRUPTED
} InputResult;

typedef struct ShellState ShellState;

InputResult input_read_line(char **line, ShellState *state);

#endif
