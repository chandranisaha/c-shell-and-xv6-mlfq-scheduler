#ifndef CSHELL_INPUT_H
#define CSHELL_INPUT_H

typedef enum {
    INPUT_LINE,
    INPUT_EOF,
    INPUT_ERROR,
    INPUT_TOO_LONG,
    INPUT_INTERRUPTED
} InputResult;

/* Allocates one input line. The caller owns *line after INPUT_LINE. */
InputResult input_read_line(char **line);

#endif
