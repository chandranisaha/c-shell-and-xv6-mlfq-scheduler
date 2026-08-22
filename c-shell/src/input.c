#include "input.h"

#include <stdio.h>
#include <stdlib.h>

InputResult input_read_line(char **line)
{
    if (line == NULL) {
        return INPUT_ERROR;
    }

    *line = NULL;
    size_t capacity = 128;
    size_t length = 0;
    *line = malloc(capacity);
    if (*line == NULL) {
        return INPUT_ERROR;
    }

    for (;;) {
        int value = fgetc(stdin);
        if (value == EOF) {
            if (length == 0) {
                free(*line);
                *line = NULL;
                return ferror(stdin) ? INPUT_ERROR : INPUT_EOF;
            }
            break;
        }

        char character = (char)value;
        if (character == '\n') {
            break;
        }

        if (length + 1 >= capacity) {
            size_t new_capacity = capacity * 2;
            char *expanded = realloc(*line, new_capacity);
            if (expanded == NULL) {
                free(*line);
                *line = NULL;
                return INPUT_ERROR;
            }
            *line = expanded;
            capacity = new_capacity;
        }

        (*line)[length] = character;
        length++;
    }

    (*line)[length] = '\0';
    return INPUT_LINE;
}
