#ifndef CSHELL_INPUT_H
#define CSHELL_INPUT_H

typedef enum {
    INPUT_LINE,
    INPUT_EOF,
    INPUT_ERROR
} InputResult;

/* Allocates one input line. The caller owns *line after INPUT_LINE. */
InputResult input_read_line(char **line);

#endif
