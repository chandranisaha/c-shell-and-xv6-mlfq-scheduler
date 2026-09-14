#include "spy.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Plenty for any process we are likely to look at, and bounded so the
 * descriptor list can live on the stack. */
#define OPEN_MAX_GUESS 4096
#define MAX_MAPPINGS   1024

/* Same strict integer parser as ping.c and resume.c: at least one digit,
 * no sign, no trailing junk, no overflow. */
static bool parse_non_negative_long(const char *text, long *out)
{
    if (text == NULL || text[0] == '\0') {
        return false;
    }
    errno = 0;
    char *end = NULL;
    long value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value < 0) {
        return false;
    }
    *out = value;
    return true;
}

/* lsof-style type letters for whatever the descriptor actually points at.
 * Sockets and pipes are reported with what stat() says rather than being
 * skipped - the assignment puts network *types* out of scope, not the
 * entries themselves. */
static const char *type_of(mode_t mode)
{
    if (S_ISREG(mode)) {
        return "REG";
    }
    if (S_ISDIR(mode)) {
        return "DIR";
    }
    if (S_ISCHR(mode)) {
        return "CHR";
    }
    if (S_ISBLK(mode)) {
        return "BLK";
    }
    if (S_ISFIFO(mode)) {
        return "FIFO";
    }
    if (S_ISLNK(mode)) {
        return "LNK";
    }
    if (S_ISSOCK(mode)) {
        return "SOCK";
    }
    return "unknown";
}

/* stat() the /proc entry rather than the resolved path: it follows to the
 * real object, and it still works for things with no name in the
 * filesystem, like a pipe. Returns "unknown" if it cannot be stat'ed. */
static const char *type_of_proc_entry(const char *proc_path)
{
    struct stat metadata;

    if (stat(proc_path, &metadata) != 0) {
        return "unknown";
    }
    return type_of(metadata.st_mode);
}

static void print_row(long pid, const char *fd, const char *type,
                      const char *path)
{
    printf("%-6ld %-5s %-6s %s\n", pid, fd, type, path);
}

/* readlink() into a caller-owned buffer, NUL-terminating by hand since
 * readlink does not. Returns false if the link cannot be read, which is
 * normal for a process we do not own. */
static bool read_proc_link(const char *proc_path, char *out, size_t size)
{
    ssize_t length = readlink(proc_path, out, size - 1);

    if (length < 0) {
        return false;
    }
    out[length] = '\0';
    return true;
}

/* One row per *unique* pathname in /proc/<pid>/maps. The kernel emits one
 * line per segment, so every library shows up four or five times with
 * different permissions; the spec explicitly wants each printed once.
 *
 * `exclude` is the executable, already printed as txt. It is mapped like
 * anything else, but the writeup's example lists /usr/bin/sleep under txt
 * and never again under mem, which is what lsof does too.
 *
 * A maps line looks like:
 *   7f3a.. -7f3a.. r-xp 00000000 08:01 1234   /usr/lib/libc.so.6
 * The path is everything after the sixth field, and it is only present for
 * file-backed mappings - anonymous memory and [heap]/[stack] have either
 * nothing or a bracketed name there, and both are skipped.
 */
static void print_mappings(long pid, const char *exclude)
{
    char maps_path[PATH_MAX];

    snprintf(maps_path, sizeof(maps_path), "/proc/%ld/maps", pid);

    FILE *maps = fopen(maps_path, "r");
    if (maps == NULL) {
        return;
    }

    char *seen[MAX_MAPPINGS];
    size_t seen_count = 0;
    char line[PATH_MAX * 2];

    while (fgets(line, sizeof(line), maps) != NULL &&
           seen_count < MAX_MAPPINGS) {
        /* Walk past the six fixed fields to whatever remains. */
        char *cursor = line;
        for (int field = 0; field < 5; field++) {
            cursor = strchr(cursor, ' ');
            if (cursor == NULL) {
                break;
            }
            while (*cursor == ' ') {
                cursor++;
            }
        }
        if (cursor == NULL) {
            continue;
        }

        char *newline = strchr(cursor, '\n');
        if (newline != NULL) {
            *newline = '\0';
        }

        /* Only file-backed mappings have a real path here. */
        if (cursor[0] != '/') {
            continue;
        }
        if (exclude != NULL && strcmp(cursor, exclude) == 0) {
            continue;
        }

        bool duplicate = false;
        for (size_t index = 0; index < seen_count; index++) {
            if (strcmp(seen[index], cursor) == 0) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            continue;
        }

        seen[seen_count] = strdup(cursor);
        if (seen[seen_count] == NULL) {
            break;
        }
        seen_count++;

        /* A mapping's path is a real filesystem path, so it can be stat'ed
         * directly - no need to go back through /proc for the type. */
        print_row(pid, "mem", type_of_proc_entry(cursor), cursor);
    }

    fclose(maps);

    for (size_t index = 0; index < seen_count; index++) {
        free(seen[index]);
    }
}

static int descriptor_compare(const void *left, const void *right)
{
    long a = *(const long *)left;
    long b = *(const long *)right;

    return a < b ? -1 : (a > b ? 1 : 0);
}

/* Numeric descriptors, ascending. readdir() hands them back in whatever
 * order the filesystem likes, so they are collected and sorted rather than
 * printed as they arrive - "0, 1, 2" in the writeup's example is clearly
 * meant to be in order. */
static void print_descriptors(long pid)
{
    char dir_path[PATH_MAX];

    snprintf(dir_path, sizeof(dir_path), "/proc/%ld/fd", pid);

    DIR *handle = opendir(dir_path);
    if (handle == NULL) {
        return;
    }

    long numbers[OPEN_MAX_GUESS];
    size_t count = 0;
    struct dirent *entry;

    while ((entry = readdir(handle)) != NULL && count < OPEN_MAX_GUESS) {
        long number;
        if (parse_non_negative_long(entry->d_name, &number)) {
            numbers[count++] = number;
        }
    }
    closedir(handle);

    qsort(numbers, count, sizeof(*numbers), descriptor_compare);

    for (size_t index = 0; index < count; index++) {
        char proc_path[PATH_MAX];
        char target[PATH_MAX];
        char label[32];

        snprintf(proc_path, sizeof(proc_path), "/proc/%ld/fd/%ld", pid,
                 numbers[index]);
        /* The descriptor can be gone by now - opendir() itself used one,
         * and the process is running while we look at it. */
        if (!read_proc_link(proc_path, target, sizeof(target))) {
            continue;
        }
        snprintf(label, sizeof(label), "%ld", numbers[index]);
        print_row(pid, label, type_of_proc_entry(proc_path), target);
    }
}

static bool process_exists(long pid)
{
    char path[PATH_MAX];
    struct stat metadata;

    snprintf(path, sizeof(path), "/proc/%ld", pid);
    return stat(path, &metadata) == 0 && S_ISDIR(metadata.st_mode);
}

int spy_execute(const ShellState *state, const TokenList *tokens)
{
    if (state == NULL || tokens == NULL || tokens->count == 0 ||
        tokens->items[0].type != TOKEN_WORD ||
        strcmp(tokens->items[0].value, "spy") != 0) {
        return -1;
    }

    /* At most one pid. Two or more is a syntax error whether or not they
     * would have resolved. */
    if (tokens->count > 2) {
        fprintf(stderr, "spy: invalid syntax\n");
        return 0;
    }

    long pid = state->shell_pid;

    if (tokens->count == 2) {
        if (tokens->items[1].type != TOKEN_WORD ||
            !parse_non_negative_long(tokens->items[1].value, &pid)) {
            fprintf(stderr, "spy: invalid syntax\n");
            return 0;
        }
    }

    if (!process_exists(pid)) {
        fprintf(stderr, "spy: no such process\n");
        return 0;
    }

    printf("PID    FD    TYPE   PATH\n");

    char proc_path[PATH_MAX];
    char target[PATH_MAX];

    /* cwd and the executable text, in the order the writeup's example
     * prints them. */
    snprintf(proc_path, sizeof(proc_path), "/proc/%ld/cwd", pid);
    if (read_proc_link(proc_path, target, sizeof(target))) {
        print_row(pid, "cwd", type_of_proc_entry(proc_path), target);
    }

    char executable[PATH_MAX];
    bool have_executable = false;

    snprintf(proc_path, sizeof(proc_path), "/proc/%ld/exe", pid);
    if (read_proc_link(proc_path, target, sizeof(target))) {
        print_row(pid, "txt", type_of_proc_entry(proc_path), target);
        snprintf(executable, sizeof(executable), "%s", target);
        have_executable = true;
    }

    print_mappings(pid, have_executable ? executable : NULL);
    print_descriptors(pid);

    return 0;
}
