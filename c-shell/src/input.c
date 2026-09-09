#include "input.h"
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
            /* Q55: on a terminal, read() returning 0 is always EOF, even
             * with a half-typed line already collected. Ctrl-D on a
             * non-empty line does not produce this - it just flushes what
             * was typed, so read() returns those bytes and we loop. Only
             * a *second* Ctrl-D, now on an empty tty buffer, gets here,
             * and that means exit; the half-typed line is discarded
             * rather than executed.
             *
             * Off a terminal there is no Ctrl-D and no second chance, so
             * a final line with no trailing newline (a script piped in,
             * say) is still delivered rather than silently dropped. */
            if (length == 0 || isatty(STDIN_FILENO)) {
                free(*line);
                *line = NULL;
                return INPUT_EOF;
            }
            break;
        }

        if (amount < 0) {
            if (errno == EINTR) {
                /* Ctrl-C reached the shell itself, which only happens when
                 * no foreground job owns the terminal - i.e. the user hit
                 * it at an idle prompt. The tty driver has already flushed
                 * its input queue, so anything half-typed is gone; hand
                 * back INPUT_INTERRUPTED and let the main loop draw a
                 * fresh prompt (Q54). */
                if (signals_interrupt_pending()) {
                    signals_clear_interrupt();
                    free(*line);
                    *line = NULL;
                    return INPUT_INTERRUPTED;
                }
                /* Otherwise a signal (in practice SIGCHLD) landed while we were
                 * blocked. Q50/Q58: bash reaps in the handler but prints
                 * the completion message right before the *next* prompt,
                 * never mid-line - and that is what the main loop already
                 * does, once per iteration just above prompt_print(). So
                 * there is deliberately nothing to do here but resume.
                 *
                 * Printing here instead was the old behaviour, and it can
                 * never be made correct in canonical mode: whatever the
                 * user has typed so far has been echoed by the kernel tty
                 * driver but not delivered to us (no newline yet), so
                 * `length` is still 0 and we cannot redraw the typed text
                 * that Q58 requires us to redraw. Resuming the read leaves
                 * the kernel's line buffer untouched, so the interruption
                 * is completely invisible. */
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
