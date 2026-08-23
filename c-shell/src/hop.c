#include "hop.h"

#include "path_utils.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int is_frecency_query(const char *argument)
{
    return argument[0] != '\0' && argument[0] != '.' && argument[0] != '/' &&
           argument[0] != '~' && argument[0] != '-' &&
           strchr(argument, '/') == NULL;
}

static int change_directory(ShellState *state, const char *argument)
{
    char current_directory[PATH_MAX];
    if (getcwd(current_directory, sizeof(current_directory)) == NULL) {
        fprintf(stderr, "hop: no such directory\n");
        return -1;
    }

    char target[PATH_MAX];
    PathResolveStatus status = resolve_path(state, argument, target,
                                             sizeof(target));
    if (status == PATH_RESOLVE_NOT_FOUND && is_frecency_query(argument)) {
        int found = frecency_find_best_match(&state->frecency, argument,
                                             target, sizeof(target));
        if (found <= 0) {
            status = PATH_RESOLVE_NOT_FOUND;
        } else {
            status = PATH_RESOLVE_SUCCESS;
        }
    }

    if (status != PATH_RESOLVE_SUCCESS || chdir(target) != 0) {
        fprintf(stderr, "hop: no such directory\n");
        return -1;
    }

    int written = snprintf(state->previous_directory,
                           sizeof(state->previous_directory), "%s",
                           current_directory);
    if (written < 0 || (size_t)written >= sizeof(state->previous_directory)) {
        return -1;
    }

    char entered_directory[PATH_MAX];
    if (getcwd(entered_directory, sizeof(entered_directory)) != NULL) {
        (void)frecency_add_visit(&state->frecency, entered_directory);
    }
    return 0;
}

int hop_execute(ShellState *state, const TokenList *tokens)
{
    if (state == NULL || tokens == NULL || tokens->count == 0 ||
        tokens->items[0].type != TOKEN_WORD ||
        strcmp(tokens->items[0].value, "hop") != 0) {
        return -1;
    }

    for (size_t index = 1; index < tokens->count; index++) {
        if (tokens->items[index].type != TOKEN_WORD) {
            return -1;
        }
    }

    if (tokens->count == 1) {
        (void)change_directory(state, "~");
        return 0;
    }

    for (size_t index = 1; index < tokens->count; index++) {
        (void)change_directory(state, tokens->items[index].value);
    }
    return 0;
}
