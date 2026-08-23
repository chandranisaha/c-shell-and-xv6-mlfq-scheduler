#include "frecency.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int database_grow(FrecencyDB *database)
{
    if (database->count < database->capacity) {
        return 0;
    }

    size_t new_capacity = database->capacity == 0 ? 16 : database->capacity * 2;
    FrecencyEntry *expanded = realloc(database->entries,
                                      new_capacity * sizeof(*expanded));
    if (expanded == NULL) {
        return -1;
    }

    database->entries = expanded;
    database->capacity = new_capacity;
    return 0;
}

static int save_database(const FrecencyDB *database)
{
    FILE *file = fopen(database->storage_path, "w");
    if (file == NULL) {
        return -1;
    }

    for (size_t index = 0; index < database->count; index++) {
        const FrecencyEntry *entry = &database->entries[index];
        if (fprintf(file, "%s\t%.17g\t%lld\n", entry->path, entry->score,
                    (long long)entry->last_accessed) < 0) {
            fclose(file);
            return -1;
        }
    }

    return fclose(file) == 0 ? 0 : -1;
}

static void load_database(FrecencyDB *database)
{
    FILE *file = fopen(database->storage_path, "r");
    if (file == NULL) {
        return;
    }

    char line[PATH_MAX + 128];
    while (fgets(line, sizeof(line), file) != NULL) {
        char *path = strtok(line, "\t\n");
        char *score_text = strtok(NULL, "\t\n");
        char *time_text = strtok(NULL, "\t\n");
        if (path == NULL || score_text == NULL || time_text == NULL ||
            database_grow(database) != 0) {
            continue;
        }

        FrecencyEntry *entry = &database->entries[database->count];
        int written = snprintf(entry->path, sizeof(entry->path), "%s", path);
        if (written < 0 || (size_t)written >= sizeof(entry->path)) {
            continue;
        }
        entry->score = strtod(score_text, NULL);
        entry->last_accessed = (time_t)strtoll(time_text, NULL, 10);
        database->count++;
    }

    fclose(file);
}

int frecency_init(FrecencyDB *database, const char *home_directory)
{
    if (database == NULL || home_directory == NULL) {
        return -1;
    }

    database->entries = NULL;
    database->count = 0;
    database->capacity = 0;

    const char *home = getenv("HOME");
    if (home == NULL || home[0] == '\0') {
        home = home_directory;
    }
    int written = snprintf(database->storage_path,
                           sizeof(database->storage_path),
                           "%s/.cshell_frecency", home);
    if (written < 0 || (size_t)written >= sizeof(database->storage_path)) {
        return -1;
    }

    load_database(database);
    return 0;
}

void frecency_destroy(FrecencyDB *database)
{
    if (database == NULL) {
        return;
    }
    free(database->entries);
    database->entries = NULL;
    database->count = 0;
    database->capacity = 0;
}

int frecency_add_visit(FrecencyDB *database, const char *path)
{
    if (database == NULL || path == NULL) {
        return -1;
    }

    FrecencyEntry *entry = NULL;
    for (size_t index = 0; index < database->count; index++) {
        if (strcmp(database->entries[index].path, path) == 0) {
            entry = &database->entries[index];
            break;
        }
    }

    if (entry == NULL) {
        if (database_grow(database) != 0) {
            return -1;
        }
        entry = &database->entries[database->count];
        int written = snprintf(entry->path, sizeof(entry->path), "%s", path);
        if (written < 0 || (size_t)written >= sizeof(entry->path)) {
            return -1;
        }
        entry->score = 0.0;
        database->count++;
    }

    entry->score += 1.0;
    entry->last_accessed = time(NULL);
    return save_database(database);
}

int frecency_find_best_match(const FrecencyDB *database, const char *query,
                             char *out_path, size_t out_size)
{
    if (database == NULL || query == NULL || out_path == NULL || out_size == 0) {
        return -1;
    }

    time_t now = time(NULL);
    const FrecencyEntry *best = NULL;
    double best_rank = -1.0;

    for (size_t index = 0; index < database->count; index++) {
        const FrecencyEntry *entry = &database->entries[index];
        struct stat metadata;
        if (strstr(entry->path, query) == NULL ||
            stat(entry->path, &metadata) != 0 || !S_ISDIR(metadata.st_mode)) {
            continue;
        }

        double age = difftime(now, entry->last_accessed);
        if (age < 0.0) {
            age = 0.0;
        }
        double rank = entry->score / (1.0 + age / 86400.0);
        if (best == NULL || rank > best_rank ||
            (rank == best_rank && entry->last_accessed > best->last_accessed) ||
            (rank == best_rank && entry->last_accessed == best->last_accessed &&
             strcmp(entry->path, best->path) < 0)) {
            best = entry;
            best_rank = rank;
        }
    }

    if (best == NULL) {
        return 0;
    }

    int written = snprintf(out_path, out_size, "%s", best->path);
    return written < 0 || (size_t)written >= out_size ? -1 : 1;
}
