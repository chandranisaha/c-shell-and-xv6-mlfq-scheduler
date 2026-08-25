#include "reveal.h"

#include "path_utils.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct {
    char *name;
    int is_directory;
} DirectoryEntry;

static void free_entries(DirectoryEntry *entries, size_t count)
{
    for (size_t index = 0; index < count; index++) {
        free(entries[index].name);
    }
    free(entries);
}

static int entry_compare(const void *left, const void *right)
{
    const DirectoryEntry *left_entry = left;
    const DirectoryEntry *right_entry = right;
    return strcmp(left_entry->name, right_entry->name);
}

static int collect_entries(const char *directory, int show_hidden,
                           DirectoryEntry **out_entries, size_t *out_count)
{
    DIR *handle = opendir(directory);
    if (handle == NULL) {
        return -1;
    }

    DirectoryEntry *entries = NULL;
    size_t count = 0;
    size_t capacity = 0;
    struct dirent *entry;
    while ((entry = readdir(handle)) != NULL) {
        if (!show_hidden && entry->d_name[0] == '.') {
            continue;
        }

        if (count == capacity) {
            size_t new_capacity = capacity == 0 ? 16 : capacity * 2;
            DirectoryEntry *expanded = realloc(entries,
                                                new_capacity * sizeof(*expanded));
            if (expanded == NULL) {
                closedir(handle);
                free_entries(entries, count);
                return -1;
            }
            entries = expanded;
            capacity = new_capacity;
        }

        char full_path[PATH_MAX * 2];
        int written = snprintf(full_path, sizeof(full_path), "%s/%s",
                               directory, entry->d_name);
        if (written < 0 || (size_t)written >= sizeof(full_path)) {
            closedir(handle);
            free_entries(entries, count);
            return -1;
        }

        struct stat metadata;
        if (stat(full_path, &metadata) != 0) {
            metadata.st_mode = 0;
        }

        entries[count].name = strdup(entry->d_name);
        if (entries[count].name == NULL) {
            closedir(handle);
            free_entries(entries, count);
            return -1;
        }
        entries[count].is_directory = S_ISDIR(metadata.st_mode);
        count++;
    }

    closedir(handle);
    qsort(entries, count, sizeof(*entries), entry_compare);
    *out_entries = entries;
    *out_count = count;
    return 0;
}

static int reveal_directory(const char *directory, const char *display_prefix,
                            int show_hidden, int recursive)
{
    DirectoryEntry *entries = NULL;
    size_t count = 0;
    if (collect_entries(directory, show_hidden, &entries, &count) != 0) {
        return -1;
    }

    for (size_t index = 0; index < count; index++) {
        const DirectoryEntry *entry = &entries[index];
        printf("%s%s%s\n", display_prefix, entry->name,
               entry->is_directory ? "/" : "");

        if (recursive && entry->is_directory &&
            strcmp(entry->name, ".") != 0 &&
            strcmp(entry->name, "..") != 0) {
            char child_path[PATH_MAX * 2];
            char child_prefix[PATH_MAX * 2];
            int path_written = snprintf(child_path, sizeof(child_path), "%s/%s",
                                        directory, entry->name);
            int prefix_written = snprintf(child_prefix, sizeof(child_prefix),
                                          "%s%s/", display_prefix,
                                          entry->name);
            if (path_written < 0 || prefix_written < 0 ||
                (size_t)path_written >= sizeof(child_path) ||
                (size_t)prefix_written >= sizeof(child_prefix) ||
                reveal_directory(child_path, child_prefix, show_hidden,
                                 recursive) != 0) {
                free_entries(entries, count);
                return -1;
            }
        }
    }

    free_entries(entries, count);
    return 0;
}

int reveal_execute(const ShellState *state, const TokenList *tokens)
{
    if (state == NULL || tokens == NULL || tokens->count == 0 ||
        tokens->items[0].type != TOKEN_WORD ||
        strcmp(tokens->items[0].value, "reveal") != 0) {
        return -1;
    }

    int show_hidden = 0;
    int recursive = 0;
    const char *target = ".";
    size_t positional_count = 0;

    for (size_t index = 1; index < tokens->count; index++) {
        if (tokens->items[index].type != TOKEN_WORD) {
            return -1;
        }

        const char *argument = tokens->items[index].value;
        if (argument[0] == '-' && argument[1] != '\0') {
            for (size_t flag = 1; argument[flag] != '\0'; flag++) {
                if (argument[flag] == 'a') {
                    show_hidden = 1;
                } else if (argument[flag] == 't') {
                    recursive = 1;
                } else {
                    printf("reveal: invalid syntax\n");
                    return 0;
                }
            }
        } else {
            positional_count++;
            if (positional_count > 1) {
                printf("reveal: invalid syntax\n");
                return 0;
            }
            target = argument;
        }
    }

    char directory[PATH_MAX];
    if (resolve_path(state, target, directory, sizeof(directory)) !=
        PATH_RESOLVE_SUCCESS) {
        printf("reveal: no such directory\n");
        return 0;
    }

    if (reveal_directory(directory, "", show_hidden, recursive) != 0) {
        printf("reveal: no such directory\n");
    }
    return 0;
}
