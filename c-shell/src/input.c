#include "input.h"
#include "jobs.h"
#include "prompt.h"
#include "signals.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define INPUT_MAX_LENGTH 1024

InputResult input_read_line(char **line, ShellState *state)
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
        char character;
        ssize_t amount = read(STDIN_FILENO, &character, 1);

        if (amount == 0) {
            if (length == 0) {
                free(*line);
                *line = NULL;
                return INPUT_EOF;
            }
            break;
        }

        if (amount < 0) {
            if (errno == EINTR) {
                if (state != NULL) {
                    int interactive = isatty(STDIN_FILENO);
                    /* The terminal is in canonical mode: whatever the user
                     * has typed so far on the current line is only echoed
                     * by the kernel tty driver, not yet delivered to us (no
                     * newline yet), so `length` is still 0 here and we have
                     * no way to know what is on screen. Do NOT erase the
                     * current line - doing so would wipe out those
                     * kernel-echoed, not-yet-delivered characters and
                     * desync the display from the pending input. Instead,
                     * simply move past the current line with a newline so
                     * the notification (and any already-typed text above
                     * it) stay intact, matching the assignment's own
                     * example transcript. */
                    int reaped = jobs_reap_background(state, interactive);
                    signals_clear();
                    if (reaped > 0 && interactive) {
                        if (prompt_print(state) != 0) {
                            free(*line);
                            *line = NULL;
                            return INPUT_ERROR;
                        }
                    }
                }
                continue;
            }
            free(*line);
            *line = NULL;
            return INPUT_ERROR;
        }

        if (character == '\n') {
            break;
        }

        if (length >= INPUT_MAX_LENGTH) {
            do {
                amount = read(STDIN_FILENO, &character, 1);
                if (amount < 0) {
                    free(*line);
                    *line = NULL;
                    return INPUT_ERROR;
                }
            } while (amount > 0 && character != '\n');
            free(*line);
            *line = NULL;
            return INPUT_TOO_LONG;
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
