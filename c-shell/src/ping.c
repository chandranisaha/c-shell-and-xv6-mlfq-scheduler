#include "ping.h"

#include "jobs.h"

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

/* Same non-negative-integer parser as resume.c: no sign, no leading/
 * trailing junk, at least one digit, no overflow. */
static bool parse_non_negative_long(const char *text, long *out)
{
    if (text == NULL || text[0] == '\0') {
        return false;
    }
    errno = 0;
    char *end = NULL;
    long value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value < 0) {
        return false;
    }
    *out = value;
    return true;
}

int ping_execute(ShellState *state, const TokenList *tokens)
{
    if (state == NULL || tokens == NULL || tokens->count == 0 ||
        tokens->items[0].type != TOKEN_WORD ||
        strcmp(tokens->items[0].value, "ping") != 0) {
        return -1;
    }

    for (size_t index = 1; index < tokens->count; index++) {
        if (tokens->items[index].type != TOKEN_WORD) {
            printf("ping: invalid syntax\n");
            return 0;
        }
    }

    if (tokens->count != 3) {
        printf("ping: invalid syntax\n");
        return 0;
    }

    const char *target_arg = tokens->items[1].value;
    const char *signal_arg = tokens->items[2].value;

    /* signal_number is validated before target is looked up, and negative
     * numbers are a syntax error rather than a value to be reduced by the
     * modulo. */
    long typed_signal = 0;
    if (!parse_non_negative_long(signal_arg, &typed_signal)) {
        printf("ping: invalid syntax\n");
        return 0;
    }
    int actual_signal = (int)(typed_signal % 64);

    bool found = false;
    pid_t target_pid = 0;
    Job *target_job = NULL;

    if (target_arg[0] == '%') {
        long job_number = 0;
        if (parse_non_negative_long(target_arg + 1, &job_number)) {
            target_job = job_find_by_number(state, (int)job_number);
            found = target_job != NULL;
        }
    } else {
        long pid_value = 0;
        if (parse_non_negative_long(target_arg, &pid_value)) {
            /* Only pids this shell itself spawned and is still tracking
             * count - a pid that merely exists on the system is unknown. */
            if (job_find_by_pid(state, (pid_t)pid_value) != NULL) {
                target_pid = (pid_t)pid_value;
                found = true;
            }
        }
    }

    if (!found) {
        printf("ping: no such process found\n");
        return 0;
    }

    if (target_job != NULL) {
        (void)kill(-target_job->pgid, actual_signal);
    } else {
        (void)kill(target_pid, actual_signal);
    }

    printf("Sent signal %s to %s\n", signal_arg, target_arg);
    fflush(stdout);
    return 0;
}
