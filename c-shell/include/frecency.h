#ifndef CSHELL_FRECENCY_H
#define CSHELL_FRECENCY_H

#include <limits.h>
#include <stddef.h>
#include <time.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef struct {
    char path[PATH_MAX];
    double score;
    time_t last_accessed;
} FrecencyEntry;

typedef struct {
    FrecencyEntry *entries;
    size_t count;
    size_t capacity;
    char storage_path[PATH_MAX];
} FrecencyDB;

int frecency_init(FrecencyDB *database, const char *home_directory);
void frecency_destroy(FrecencyDB *database);
int frecency_add_visit(FrecencyDB *database, const char *path);
int frecency_find_best_match(const FrecencyDB *database, const char *query,
                             char *out_path, size_t out_size);

#endif
