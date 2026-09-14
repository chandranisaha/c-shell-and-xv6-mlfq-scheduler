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

    snprintf(proc_path, sizeof(proc_path), "/proc/%ld/exe", pid);
    if (read_proc_link(proc_path, target, sizeof(target))) {
        print_row(pid, "txt", type_of_proc_entry(proc_path), target);
    }

    return 0;
}
