#ifndef CSHELL_SHELL_H
#define CSHELL_SHELL_H

#include <limits.h>
#include <unistd.h>

#include "frecency.h"
#include "jobs.h"

#ifndef LOGIN_NAME_MAX
#define LOGIN_NAME_MAX 256
#endif

#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX 256
#endif

struct ShellState {
    char home_directory[PATH_MAX];
    char previous_directory[PATH_MAX];
    char username[LOGIN_NAME_MAX];
    char hostname[HOST_NAME_MAX];
    FrecencyDB frecency;
    Job *jobs;
    int next_job_number;
    pid_t shell_pgid;
    pid_t shell_pid;
    pid_t foreground_pgid;
    int terminal_fd;
};

int shell_state_init(ShellState *state);
void shell_state_destroy(ShellState *state);

#endif
