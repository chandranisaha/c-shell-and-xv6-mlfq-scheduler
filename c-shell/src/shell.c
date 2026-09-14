#include "shell.h"

#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int copy_username(char *destination, size_t destination_size)
{
    struct passwd *user = getpwuid(geteuid());
    if (user == NULL || user->pw_name == NULL) {
        return -1;
    }
    const char *username = user->pw_name;

    int written = snprintf(destination, destination_size, "%s", username);
    return written < 0 || (size_t)written >= destination_size ? -1 : 0;
}

int shell_state_init(ShellState *state)
{
    if (state == NULL) {
        return -1;
    }

    if (getcwd(state->home_directory, sizeof(state->home_directory)) == NULL) {
        return -1;
    }
    state->previous_directory[0] = '\0';
    state->jobs = NULL;
    state->next_job_number = 1;
    state->shell_pgid = getpgrp();
    state->shell_pid = getpid();
    state->foreground_pgid = 0;
    state->terminal_fd = STDIN_FILENO;

    if (copy_username(state->username, sizeof(state->username)) != 0) {
        return -1;
    }

    if (gethostname(state->hostname, sizeof(state->hostname)) != 0) {
        return -1;
    }
    state->hostname[sizeof(state->hostname) - 1] = '\0';

    if (frecency_init(&state->frecency, state->home_directory) != 0) {
        return -1;
    }

    return 0;
}

void shell_state_destroy(ShellState *state)
{
    if (state != NULL) {
        /* E2: whenever the shell exits with background or stopped jobs
         * still tracked, every one of them gets SIGHUP before we tear down
         * our own state, without waiting for them to react to it. */
        jobs_hangup_all(state);
        Job *job = state->jobs;
        while (job != NULL) {
            Job *next = job->next;
            job_destroy(job);
            job = next;
        }
        frecency_destroy(&state->frecency);
    }
}
