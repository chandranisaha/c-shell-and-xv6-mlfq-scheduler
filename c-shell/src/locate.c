#include "locate.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static int absolute_path(const char *path, char *out_path, size_t out_size)
{
    if (path[0] == '/') {
        int written = snprintf(out_path, out_size, "%s", path);
        return written < 0 || (size_t)written >= out_size ? -1 : 0;
    }

    char current_directory[PATH_MAX];
    if (getcwd(current_directory, sizeof(current_directory)) == NULL) {
        return -1;
    }
    int written = snprintf(out_path, out_size, "%s/%s", current_directory,
                           path);
    return written < 0 || (size_t)written >= out_size ? -1 : 0;
}

static int is_executable_file(const char *path)
{
    struct stat metadata;
    return access(path, X_OK) == 0 && stat(path, &metadata) == 0 &&
           S_ISREG(metadata.st_mode);
}

static int print_match(const char *path)
{
    if (!is_executable_file(path)) {
        return 0;
    }

    char output_path[PATH_MAX];
    if (absolute_path(path, output_path, sizeof(output_path)) != 0) {
        return 0;
    }

    printf("%s\n", output_path);
    return 1;
}

static int locate_one(const char *name)
{
    int found = 0;
    char current_directory[PATH_MAX];
    if (getcwd(current_directory, sizeof(current_directory)) != NULL) {
        char current_path[PATH_MAX * 2];
        int written = snprintf(current_path, sizeof(current_path), "%s/%s",
                               current_directory, name);
        if (written >= 0 && (size_t)written < sizeof(current_path)) {
            found += print_match(current_path);
        }
    }

    const char *path_variable = getenv("PATH");
    if (path_variable != NULL) {
        const char *component_start = path_variable;
        for (;;) {
            const char *separator = strchr(component_start, ':');
            size_t component_length = separator == NULL
                                          ? strlen(component_start)
                                          : (size_t)(separator - component_start);
            char directory[PATH_MAX];
            if (component_length == 0) {
                snprintf(directory, sizeof(directory), ".");
            } else if (component_length < sizeof(directory)) {
                memcpy(directory, component_start, component_length);
                directory[component_length] = '\0';
            } else {
                directory[0] = '\0';
            }

            if (directory[0] != '\0') {
                char candidate[PATH_MAX * 2];
                int written = snprintf(candidate, sizeof(candidate), "%s/%s",
                                        directory, name);
                if (written >= 0 && (size_t)written < sizeof(candidate)) {
                    found += print_match(candidate);
                }
            }

            if (separator == NULL) {
                break;
            }
            component_start = separator + 1;
        }
    }

    if (found == 0) {
        fprintf(stderr, "locate: command not found (%s)\n", name);
    }

    return 0;
}

int locate_execute(const ShellState *state, const TokenList *tokens)
{
    (void)state;
    if (tokens == NULL || tokens->count == 0 ||
        tokens->items[0].type != TOKEN_WORD ||
        strcmp(tokens->items[0].value, "locate") != 0) {
        return -1;
    }

    if (tokens->count == 1) {
        fprintf(stderr, "locate: invalid syntax\n");
        return 0;
    }

    for (size_t index = 1; index < tokens->count; index++) {
        if (tokens->items[index].type != TOKEN_WORD) {
            return -1;
        }
        (void)locate_one(tokens->items[index].value);
    }
    return 0;
}
