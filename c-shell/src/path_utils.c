#include "path_utils.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static PathResolveStatus canonicalize_directory(const char *path,
                                                char *out_path,
                                                size_t out_size)
{
    char resolved[PATH_MAX];
    if (realpath(path, resolved) == NULL) {
        return PATH_RESOLVE_NOT_FOUND;
    }

    struct stat metadata;
    int is_directory = stat(resolved, &metadata) == 0 &&
                       S_ISDIR(metadata.st_mode);
    int written = snprintf(out_path, out_size, "%s", resolved);
    if (!is_directory) {
        return PATH_RESOLVE_NOT_FOUND;
    }
    if (written < 0 || (size_t)written >= out_size) {
        return PATH_RESOLVE_INVALID;
    }
    return PATH_RESOLVE_SUCCESS;
}

PathResolveStatus resolve_path(const ShellState *state, const char *argument,
                               char *out_path, size_t out_size)
{
    if (state == NULL || argument == NULL || out_path == NULL || out_size == 0) {
        return PATH_RESOLVE_INVALID;
    }

    if (strcmp(argument, "-") == 0) {
        if (state->previous_directory[0] == '\0') {
            return PATH_RESOLVE_NO_PREV_DIR;
        }
        return canonicalize_directory(state->previous_directory, out_path,
                                      out_size);
    }

    if (strcmp(argument, "~") == 0) {
        return canonicalize_directory(state->home_directory, out_path,
                                      out_size);
    }

    if (strncmp(argument, "~/", 2) == 0) {
        char combined[PATH_MAX * 2];
        const char *separator = state->home_directory[
                                    strlen(state->home_directory) - 1] == '/'
                                    ? ""
                                    : "/";
        int written = snprintf(combined, sizeof(combined), "%s%s%s",
                               state->home_directory, separator, argument + 2);
        if (written < 0 || (size_t)written >= sizeof(combined)) {
            return PATH_RESOLVE_INVALID;
        }
        return canonicalize_directory(combined, out_path, out_size);
    }

    return canonicalize_directory(argument, out_path, out_size);
}
