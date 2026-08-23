#ifndef CSHELL_PATH_UTILS_H
#define CSHELL_PATH_UTILS_H

#include <stddef.h>

#include "shell.h"

typedef enum {
    PATH_RESOLVE_SUCCESS,
    PATH_RESOLVE_NO_PREV_DIR,
    PATH_RESOLVE_NOT_FOUND,
    PATH_RESOLVE_INVALID
} PathResolveStatus;

PathResolveStatus resolve_path(const ShellState *state, const char *argument,
                               char *out_path, size_t out_size);

#endif
