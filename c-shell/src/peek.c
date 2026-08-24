#include "peek.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define PEEK_CHUNK_SIZE 4096

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} LineBuffer;

static void line_buffer_destroy(LineBuffer *line)
{
    free(line->data);
    line->data = NULL;
    line->length = 0;
    line->capacity = 0;
}

static int line_buffer_append(LineBuffer *line, char character)
{
    if (line->length + 1 >= line->capacity) {
        size_t new_capacity = line->capacity == 0 ? 128 : line->capacity * 2;
        char *expanded = realloc(line->data, new_capacity);
        if (expanded == NULL) {
            return -1;
        }
        line->data = expanded;
        line->capacity = new_capacity;
    }
    line->data[line->length++] = character;
    return 0;
}

static int read_line_fd(int fd, LineBuffer *line)
{
    line->data = NULL;
    line->length = 0;
    line->capacity = 0;

    for (;;) {
        char character;
        ssize_t amount = read(fd, &character, 1);
        if (amount == 0) {
            return line->length == 0 ? 0 : 1;
        }
        if (amount < 0 || line_buffer_append(line, character) != 0) {
            line_buffer_destroy(line);
            return -1;
        }
        if (character == '\n') {
            return 1;
        }
    }
}

static int line_is_nonempty(const LineBuffer *line)
{
    return line->length > 0 &&
           !(line->length == 1 && line->data[0] == '\n');
}

static int print_line(const LineBuffer *line, size_t number, int numbered)
{
    if (numbered && line_is_nonempty(line) && printf("%zu ", number) < 0) {
        return -1;
    }
    return fwrite(line->data, 1, line->length, stdout) == line->length ? 0 : -1;
}

static int print_stream(int fd, int numbered)
{
    size_t line_number = 0;
    for (;;) {
        LineBuffer line;
        int result = read_line_fd(fd, &line);
        if (result == 0) {
            return 0;
        }
        if (result < 0) {
            return -1;
        }
        if (line_is_nonempty(&line)) {
            line_number++;
        }
        size_t number = line_number;
        if (print_line(&line, number, numbered) != 0) {
            line_buffer_destroy(&line);
            return -1;
        }
        line_buffer_destroy(&line);
    }
}

static int write_all(int fd, const char *data, size_t length)
{
    size_t written_total = 0;
    while (written_total < length) {
        ssize_t written = write(fd, data + written_total,
                                length - written_total);
        if (written <= 0) {
            return -1;
        }
        written_total += (size_t)written;
    }
    return 0;
}

static int copy_stream(int fd)
{
    char buffer[PEEK_CHUNK_SIZE];
    for (;;) {
        ssize_t amount = read(fd, buffer, sizeof(buffer));
        if (amount == 0) {
            return 0;
        }
        if (amount < 0 || write_all(STDOUT_FILENO, buffer, (size_t)amount) != 0) {
            return -1;
        }
    }
}

static int count_nonempty_regular_lines(int fd, off_t size, size_t *count)
{
    if (lseek(fd, 0, SEEK_SET) == (off_t)-1) {
        return -1;
    }

    char buffer[PEEK_CHUNK_SIZE];
    int nonempty = 0;
    size_t total = 0;
    off_t position = 0;
    while (position < size) {
        size_t wanted = (size - position) < (off_t)sizeof(buffer)
                            ? (size_t)(size - position)
                            : sizeof(buffer);
        ssize_t amount = read(fd, buffer, wanted);
        if (amount <= 0) {
            return -1;
        }
        for (ssize_t index = 0; index < amount; index++) {
            if (buffer[index] == '\n') {
                if (nonempty) {
                    total++;
                }
                nonempty = 0;
            } else {
                nonempty = 1;
            }
        }
        position += amount;
    }
    if (nonempty) {
        total++;
    }
    *count = total;
    return 0;
}

static int find_previous_newline(int fd, off_t end, off_t *newline)
{
    char buffer[PEEK_CHUNK_SIZE];
    while (end > 0) {
        off_t start = end > (off_t)sizeof(buffer) ? end - sizeof(buffer) : 0;
        size_t wanted = (size_t)(end - start);
        if (lseek(fd, start, SEEK_SET) == (off_t)-1) {
            return -1;
        }
        ssize_t amount = read(fd, buffer, wanted);
        if (amount <= 0) {
            return -1;
        }
        for (ssize_t index = amount - 1; index >= 0; index--) {
            if (buffer[index] == '\n') {
                *newline = start + index;
                return 1;
            }
        }
        end = start;
    }
    return 0;
}

static int print_range(int fd, off_t start, off_t end)
{
    if (lseek(fd, start, SEEK_SET) == (off_t)-1) {
        return -1;
    }

    char buffer[PEEK_CHUNK_SIZE];
    off_t remaining = end - start;
    while (remaining > 0) {
        size_t wanted = remaining < (off_t)sizeof(buffer)
                            ? (size_t)remaining
                            : sizeof(buffer);
        ssize_t amount = read(fd, buffer, wanted);
        if (amount <= 0 || write_all(STDOUT_FILENO, buffer, (size_t)amount) != 0) {
            return -1;
        }
        remaining -= amount;
    }
    return 0;
}

static int print_reverse_regular(int fd, off_t size, int numbered)
{
    size_t remaining_number = 0;
    if (numbered && count_nonempty_regular_lines(fd, size, &remaining_number) != 0) {
        return -1;
    }

    off_t end = size;
    int terminated = 0;
    if (end > 0) {
        char last;
        if (lseek(fd, end - 1, SEEK_SET) == (off_t)-1 ||
            read(fd, &last, 1) != 1) {
            return -1;
        }
        if (last == '\n') {
            end--;
            terminated = 1;
        }
    }

    if (size > 0 && end == 0) {
        return write_all(STDOUT_FILENO, "\n", 1);
    }

    while (end > 0) {
        off_t newline = 0;
        int found = find_previous_newline(fd, end, &newline);
        if (found < 0) {
            return -1;
        }

        off_t start = found == 1 ? newline + 1 : 0;
        int nonempty = start < end;
        if (numbered && nonempty) {
            char prefix[64];
            int written = snprintf(prefix, sizeof(prefix), "%zu ",
                                    remaining_number--);
            if (written < 0 || (size_t)written >= sizeof(prefix) ||
                write_all(STDOUT_FILENO, prefix, (size_t)written) != 0) {
                return -1;
            }
        }
        if (print_range(fd, start, end) != 0) {
            return -1;
        }
        if (terminated && write_all(STDOUT_FILENO, "\n", 1) != 0) {
            return -1;
        }
        end = found == 1 ? newline : 0;
        terminated = found == 1;
    }
    return 0;
}

static int print_reverse_stream(int fd, int numbered)
{
    LineBuffer *lines = NULL;
    size_t count = 0;
    size_t capacity = 0;
    for (;;) {
        LineBuffer line;
        int result = read_line_fd(fd, &line);
        if (result == 0) {
            break;
        }
        if (result < 0) {
            for (size_t index = 0; index < count; index++) {
                line_buffer_destroy(&lines[index]);
            }
            free(lines);
            return -1;
        }
        if (count == capacity) {
            size_t new_capacity = capacity == 0 ? 16 : capacity * 2;
            LineBuffer *expanded = realloc(lines,
                                           new_capacity * sizeof(*expanded));
            if (expanded == NULL) {
                line_buffer_destroy(&line);
                for (size_t index = 0; index < count; index++) {
                    line_buffer_destroy(&lines[index]);
                }
                free(lines);
                return -1;
            }
            lines = expanded;
            capacity = new_capacity;
        }
        lines[count++] = line;
    }

    size_t remaining_number = 0;
    if (numbered) {
        for (size_t index = 0; index < count; index++) {
            if (line_is_nonempty(&lines[index])) {
                remaining_number++;
            }
        }
    }

    for (size_t index = count; index > 0; index--) {
        LineBuffer *line = &lines[index - 1];
        if (numbered && line_is_nonempty(line) &&
            printf("%zu ", remaining_number--) < 0) {
            return -1;
        }
        if (fwrite(line->data, 1, line->length, stdout) != line->length) {
            return -1;
        }
    }
    for (size_t index = 0; index < count; index++) {
        line_buffer_destroy(&lines[index]);
    }
    free(lines);
    return 0;
}

static int process_input_fd(int fd, int numbered, int reverse)
{
    struct stat metadata;
    int regular = fstat(fd, &metadata) == 0 && S_ISREG(metadata.st_mode);
    if (reverse && regular) {
        return print_reverse_regular(fd, metadata.st_size, numbered);
    }
    if (reverse) {
        return print_reverse_stream(fd, numbered);
    }
    return numbered ? print_stream(fd, 1) : copy_stream(fd);
}

static int process_file(const char *filename, int numbered, int reverse)
{
    int fd;
    int close_after = 1;
    if (strcmp(filename, "-") == 0) {
        fd = STDIN_FILENO;
        close_after = 0;
    } else {
        struct stat metadata;
        if (stat(filename, &metadata) != 0) {
            fprintf(stderr, "peek: no such file or directory\n");
            return 0;
        }
        if (S_ISDIR(metadata.st_mode)) {
            fprintf(stderr, "peek: is a directory\n");
            return 0;
        }
        fd = open(filename, O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "peek: no such file or directory\n");
            return 0;
        }
    }

    int result = process_input_fd(fd, numbered, reverse);
    if (close_after) {
        close(fd);
    }
    return result;
}

int peek_execute(const ShellState *state, const TokenList *tokens)
{
    (void)state;
    if (tokens == NULL || tokens->count == 0 ||
        tokens->items[0].type != TOKEN_WORD ||
        strcmp(tokens->items[0].value, "peek") != 0) {
        return -1;
    }

    int numbered = 0;
    int reverse = 0;
    size_t file_count = 0;
    for (size_t index = 1; index < tokens->count; index++) {
        if (tokens->items[index].type != TOKEN_WORD) {
            return -1;
        }
        const char *argument = tokens->items[index].value;
        if (argument[0] == '-' && argument[1] != '\0') {
            for (size_t flag = 1; argument[flag] != '\0'; flag++) {
                if (argument[flag] == 'n') {
                    numbered = 1;
                } else if (argument[flag] == 'r') {
                    reverse = 1;
                } else {
                    fprintf(stderr, "peek: invalid syntax\n");
                    return 0;
                }
            }
        } else {
            file_count++;
        }
    }

    if (file_count == 0) {
        (void)process_file("-", numbered, reverse);
        return 0;
    }

    for (size_t index = 1; index < tokens->count; index++) {
        const char *argument = tokens->items[index].value;
        if (argument[0] != '-' || argument[1] == '\0') {
            (void)process_file(argument, numbered, reverse);
        }
    }
    return 0;
}
