#ifndef CSHELL_SHELL_H
#define CSHELL_SHELL_H

#include <limits.h>
#include <unistd.h>

#include "frecency.h"

#ifndef LOGIN_NAME_MAX
#define LOGIN_NAME_MAX 256
#endif

#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX 256
#endif

typedef struct {
    char home_directory[PATH_MAX];
    char previous_directory[PATH_MAX];
    char username[LOGIN_NAME_MAX];
    char hostname[HOST_NAME_MAX];
    FrecencyDB frecency;
} ShellState;

int shell_state_init(ShellState *state);
void shell_state_destroy(ShellState *state);

#endif
