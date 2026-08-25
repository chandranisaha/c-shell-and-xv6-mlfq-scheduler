#include "exec.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static int executable_file(const char *path)
{
    struct stat metadata;
    return access(path, X_OK) == 0 && stat(path, &metadata) == 0 &&
           S_ISREG(metadata.st_mode);
}

static char *copy_path(const char *directory, const char *name)
{
    size_t directory_length = strlen(directory);
    size_t name_length = strlen(name);
    size_t separator = directory_length == 0 ||
                               directory[directory_length - 1] == '/'
                           ? 0
                           : 1;
    if (directory_length > SIZE_MAX - name_length - separator - 1) {
        return NULL;
    }

    size_t length = directory_length + separator + name_length;
    char *path = malloc(length + 1);
    if (path == NULL) {
        return NULL;
    }
    memcpy(path, directory, directory_length);
    if (separator != 0) {
        path[directory_length] = '/';
    }
    memcpy(path + directory_length + separator, name, name_length);
    path[length] = '\0';
    return path;
}

static char *search_path(const char *name)
{
    const char *path_variable = getenv("PATH");
    if (path_variable == NULL) {
        return NULL;
    }

    const char *component_start = path_variable;
    for (;;) {
        const char *separator = strchr(component_start, ':');
        size_t component_length = separator == NULL
                                      ? strlen(component_start)
                                      : (size_t)(separator - component_start);
        char *directory = malloc(component_length + 1);
        if (directory == NULL) {
            return NULL;
        }
        memcpy(directory, component_start, component_length);
        directory[component_length] = '\0';

        const char *search_directory = directory[0] == '\0' ? "." : directory;
        char *candidate = copy_path(search_directory, name);
        free(directory);
        if (candidate != NULL && executable_file(candidate)) {
            return candidate;
        }
        free(candidate);

        if (separator == NULL) {
            break;
        }
        component_start = separator + 1;
    }
    return NULL;
}

char *resolve_cmd_path(const char *command_name)
{
    if (command_name == NULL || command_name[0] == '\0') {
        return NULL;
    }

    const char *name = command_name;
    int path_only = 0;
    if (name[0] == '%') {
        name++;
        path_only = 1;
        if (name[0] == '\0') {
            return NULL;
        }
    }

    if (strchr(name, '/') != NULL) {
        return executable_file(name) ? strdup(name) : NULL;
    }

    if (!path_only) {
        char *current_candidate = copy_path(".", name);
        if (current_candidate != NULL && executable_file(current_candidate)) {
            return current_candidate;
        }
        free(current_candidate);
    }

    return search_path(name);
}
