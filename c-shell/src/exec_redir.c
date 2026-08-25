#include "exec.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static void input_redirection_reset(InputRedirection *input)
{
    input->input_files = NULL;
    input->input_count = 0;
    input->stream[0] = -1;
    input->stream[1] = -1;
}

static void close_input_files(InputRedirection *input)
{
    for (size_t index = 0; index < input->input_count; index++) {
        if (input->input_files[index] >= 0) {
            close(input->input_files[index]);
        }
    }
    free(input->input_files);
    input->input_files = NULL;
    input->input_count = 0;
}

int input_redirection_open(const FlatCmd *command, InputRedirection *input)
{
    if (command == NULL || input == NULL) {
        return -1;
    }
    input_redirection_reset(input);

    size_t input_count = 0;
    for (size_t index = 0; index < command->redir_count; index++) {
        if (command->redirs[index].type == TOKEN_LT) {
            input_count++;
        }
    }
    if (input_count == 0) {
        return 0;
    }

    input->input_files = malloc(input_count * sizeof(*input->input_files));
    if (input->input_files == NULL) {
        return -1;
    }
    input->input_count = input_count;
    for (size_t index = 0; index < input_count; index++) {
        input->input_files[index] = -1;
    }

    size_t file_index = 0;
    for (size_t index = 0; index < command->redir_count; index++) {
        if (command->redirs[index].type != TOKEN_LT) {
            continue;
        }
        int descriptor = open(command->redirs[index].filename, O_RDONLY);
        if (descriptor < 0) {
            fprintf(stderr, "cshell: no such file or directory\n");
            close_input_files(input);
            return 1;
        }
        input->input_files[file_index++] = descriptor;
    }

    if (pipe(input->stream) != 0) {
        close_input_files(input);
        return -1;
    }
    return 0;
}

int input_redirection_connect_child(InputRedirection *input)
{
    if (input == NULL || input->input_count == 0) {
        return 0;
    }
    if (dup2(input->stream[0], STDIN_FILENO) < 0) {
        return -1;
    }
    close(input->stream[0]);
    close(input->stream[1]);
    close_input_files(input);
    return 0;
}

static int write_all(int descriptor, const char *buffer, size_t length)
{
    size_t written_total = 0;
    while (written_total < length) {
        ssize_t written = write(descriptor, buffer + written_total,
                                length - written_total);
        if (written < 0) {
            return errno == EPIPE ? 1 : -1;
        }
        written_total += (size_t)written;
    }
    return 0;
}

static int stream_files(InputRedirection *input)
{
    char buffer[4096];
    int result = 0;
    for (size_t index = 0; index < input->input_count; index++) {
        for (;;) {
            ssize_t amount = read(input->input_files[index], buffer,
                                  sizeof(buffer));
            if (amount == 0) {
                break;
            }
            if (amount < 0) {
                result = -1;
                break;
            }
            int write_result = write_all(input->stream[1], buffer,
                                         (size_t)amount);
            if (write_result != 0) {
                result = write_result == 1 ? 0 : -1;
                break;
            }
        }
        if (result != 0) {
            break;
        }
    }
    return result;
}

int input_redirection_start_writer(InputRedirection *input)
{
    if (input == NULL || input->input_count == 0) {
        return -1;
    }
    pid_t writer = fork();
    if (writer < 0) {
        return -1;
    }
    if (writer == 0) {
        close(input->stream[0]);
        int result = stream_files(input);
        close_input_files(input);
        close(input->stream[1]);
        _exit(result == 0 ? 0 : 1);
    }

    close(input->stream[0]);
    input->stream[0] = -1;
    close(input->stream[1]);
    input->stream[1] = -1;
    close_input_files(input);
    return (int)writer;
}

void input_redirection_close_parent(InputRedirection *input)
{
    if (input == NULL) {
        return;
    }
    if (input->stream[0] >= 0) {
        close(input->stream[0]);
        input->stream[0] = -1;
    }
    if (input->stream[1] >= 0) {
        close(input->stream[1]);
        input->stream[1] = -1;
    }
    close_input_files(input);
}

int input_redirection_wait_writer(int writer_pid)
{
    int status = 0;
    return waitpid((pid_t)writer_pid, &status, 0) < 0 ? -1 : 0;
}

static void output_redirection_reset(OutputRedirection *output)
{
    output->output_files = NULL;
    output->output_count = 0;
    output->stream[0] = -1;
    output->stream[1] = -1;
}

static void close_output_files(OutputRedirection *output)
{
    for (size_t index = 0; index < output->output_count; index++) {
        if (output->output_files[index] >= 0) {
            close(output->output_files[index]);
        }
    }
    free(output->output_files);
    output->output_files = NULL;
    output->output_count = 0;
}

int output_redirection_open(const FlatCmd *command, OutputRedirection *output)
{
    if (command == NULL || output == NULL) {
        return -1;
    }
    output_redirection_reset(output);

    size_t output_count = 0;
    for (size_t index = 0; index < command->redir_count; index++) {
        if (command->redirs[index].type == TOKEN_GT ||
            command->redirs[index].type == TOKEN_GTGT) {
            output_count++;
        }
    }
    if (output_count == 0) {
        return 0;
    }

    output->output_files = malloc(output_count * sizeof(*output->output_files));
    if (output->output_files == NULL) {
        return -1;
    }
    output->output_count = output_count;
    for (size_t index = 0; index < output_count; index++) {
        output->output_files[index] = -1;
    }

    size_t file_index = 0;
    for (size_t index = 0; index < command->redir_count; index++) {
        TokenType type = command->redirs[index].type;
        if (type != TOKEN_GT && type != TOKEN_GTGT) {
            continue;
        }
        int flags = O_WRONLY | O_CREAT |
                    (type == TOKEN_GT ? O_TRUNC : O_APPEND);
        int descriptor = open(command->redirs[index].filename, flags, 0644);
        if (descriptor < 0) {
            fprintf(stderr, "cshell: unable to create file for writing\n");
            close_output_files(output);
            return 1;
        }
        output->output_files[file_index++] = descriptor;
    }

    if (pipe(output->stream) != 0) {
        close_output_files(output);
        return -1;
    }
    return 0;
}

int output_redirection_connect_child(OutputRedirection *output)
{
    if (output == NULL || output->output_count == 0) {
        return 0;
    }
    if (dup2(output->stream[1], STDOUT_FILENO) < 0) {
        return -1;
    }
    close(output->stream[0]);
    close(output->stream[1]);
    close_output_files(output);
    return 0;
}

static int stream_output(OutputRedirection *output)
{
    char buffer[4096];
    for (;;) {
        ssize_t amount = read(output->stream[0], buffer, sizeof(buffer));
        if (amount == 0) {
            return 0;
        }
        if (amount < 0) {
            return -1;
        }
        for (size_t index = 0; index < output->output_count; index++) {
            if (write_all(output->output_files[index], buffer,
                          (size_t)amount) != 0) {
                return -1;
            }
        }
    }
}

int output_redirection_start_writer(OutputRedirection *output)
{
    if (output == NULL || output->output_count == 0) {
        return -1;
    }
    pid_t writer = fork();
    if (writer < 0) {
        return -1;
    }
    if (writer == 0) {
        close(output->stream[1]);
        int result = stream_output(output);
        close(output->stream[0]);
        close_output_files(output);
        _exit(result == 0 ? 0 : 1);
    }

    close(output->stream[0]);
    output->stream[0] = -1;
    close(output->stream[1]);
    output->stream[1] = -1;
    close_output_files(output);
    return (int)writer;
}

void output_redirection_close_parent(OutputRedirection *output)
{
    if (output == NULL) {
        return;
    }
    if (output->stream[0] >= 0) {
        close(output->stream[0]);
        output->stream[0] = -1;
    }
    if (output->stream[1] >= 0) {
        close(output->stream[1]);
        output->stream[1] = -1;
    }
    close_output_files(output);
}

int output_redirection_wait_writer(int writer_pid)
{
    int status = 0;
    return waitpid((pid_t)writer_pid, &status, 0) < 0 ? -1 : 0;
}
