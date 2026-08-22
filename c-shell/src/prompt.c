#include "prompt.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int display_path(const ShellState *state, const char *cwd,
                        char *buffer, size_t buffer_size)
{
    size_t home_length = strlen(state->home_directory);

    if (strcmp(cwd, state->home_directory) == 0) {
        return snprintf(buffer, buffer_size, "~");
    }

    if (strncmp(cwd, state->home_directory, home_length) == 0 &&
        cwd[home_length] == '/') {
        return snprintf(buffer, buffer_size, "~%s", cwd + home_length);
    }

    return snprintf(buffer, buffer_size, "%s", cwd);
}

int prompt_print(const ShellState *state)
{
    if (state == NULL) {
        return -1;
    }

    char cwd[PATH_MAX];
    char path[PATH_MAX + 2];
    if (getcwd(cwd, sizeof(cwd)) == NULL ||
        display_path(state, cwd, path, sizeof(path)) < 0 ||
        printf("<%s@%s:%s> ", state->username, state->hostname, path) < 0 ||
        fflush(stdout) != 0) {
        return -1;
    }

    return 0;
}
