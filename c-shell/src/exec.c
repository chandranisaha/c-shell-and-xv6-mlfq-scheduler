#include "exec.h"

#include "builtin.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#ifdef _WIN32
extern pid_t waitpid(pid_t process_id, int *status, int options);
#else
#include <sys/wait.h>
#endif
#include <unistd.h>

#ifdef _WIN32
extern pid_t fork(void);
#else
extern char **environ;
#endif

static const char *display_name(const char *name)
{
    return name != NULL && name[0] == '%' ? name + 1 : name;
}

static int is_builtin_name(const char *name)
{
    const char *visible_name = display_name(name);
    return visible_name != NULL &&
           (strcmp(visible_name, "hop") == 0 ||
            strcmp(visible_name, "reveal") == 0 ||
            strcmp(visible_name, "peek") == 0 ||
            strcmp(visible_name, "locate") == 0);
}

static CommandLine make_stage_command(const FlatCmd *command,
                                      Token *stage_tokens)
{
    for (size_t index = 0; index < command->argc; index++) {
        stage_tokens[index] = (Token){.type = TOKEN_WORD,
                                      .value = command->argv[index]};
    }
    return (CommandLine){.tokens = {.items = stage_tokens,
                                    .count = command->argc,
                                    .capacity = command->argc}};
}

static void close_pipeline_fds(int pipe_fds[][2], size_t pipe_count)
{
    for (size_t index = 0; index < pipe_count; index++) {
        close(pipe_fds[index][0]);
        close(pipe_fds[index][1]);
    }
}

static ExecResult execute_pipeline(const Pipeline *pipeline,
                                   ShellState *state)
{
    size_t command_count = pipeline->count;
    size_t pipe_count = command_count - 1;
    int pipe_fds[MAX_PIPELINE - 1][2];
    InputRedirection inputs[MAX_PIPELINE] = {0};
    OutputRedirection outputs[MAX_PIPELINE] = {0};
    char *resolved_paths[MAX_PIPELINE] = {0};
    pid_t children[MAX_PIPELINE] = {0};
    int input_writers[MAX_PIPELINE];
    int output_writers[MAX_PIPELINE];
    for (size_t index = 0; index < command_count; index++) {
        input_writers[index] = -1;
        output_writers[index] = -1;
    }

    for (size_t index = 0; index < pipe_count; index++) {
        if (pipe(pipe_fds[index]) != 0) {
            close_pipeline_fds(pipe_fds, index);
            fprintf(stderr, "cshell: unable to create pipe\n");
            return EXEC_ERROR;
        }
    }

    for (size_t index = 0; index < command_count; index++) {
        int input_result =
            input_redirection_open(&pipeline->commands[index], &inputs[index]);
        if (input_result != 0) {
            for (size_t cleanup = 0; cleanup <= index; cleanup++) {
                input_redirection_close_parent(&inputs[cleanup]);
            }
            close_pipeline_fds(pipe_fds, pipe_count);
            return input_result == 1 ? EXEC_HANDLED : EXEC_ERROR;
        }
        int output_result = output_redirection_open(
            &pipeline->commands[index], &outputs[index]);
        if (output_result != 0) {
            for (size_t cleanup = 0; cleanup <= index; cleanup++) {
                input_redirection_close_parent(&inputs[cleanup]);
                output_redirection_close_parent(&outputs[cleanup]);
            }
            close_pipeline_fds(pipe_fds, pipe_count);
            return output_result == 1 ? EXEC_HANDLED : EXEC_ERROR;
        }
        if (!is_builtin_name(pipeline->commands[index].argv[0])) {
            resolved_paths[index] =
                resolve_cmd_path(pipeline->commands[index].argv[0]);
        }
    }

    size_t launched = 0;
    for (size_t index = 0; index < command_count; index++) {
        pid_t child = fork();
        if (child < 0) {
            fprintf(stderr, "cshell: unable to fork\n");
            close_pipeline_fds(pipe_fds, pipe_count);
            for (size_t cleanup = 0; cleanup < command_count; cleanup++) {
                input_redirection_close_parent(&inputs[cleanup]);
                output_redirection_close_parent(&outputs[cleanup]);
                free(resolved_paths[cleanup]);
            }
            for (size_t cleanup = 0; cleanup < launched; cleanup++) {
                (void)waitpid(children[cleanup], NULL, 0);
            }
            return EXEC_ERROR;
        }
        children[index] = child;
        launched++;
        if (child != 0) {
            continue;
        }

        if (index > 0 && dup2(pipe_fds[index - 1][0], STDIN_FILENO) < 0) {
            _exit(1);
        }
        if (index + 1 < command_count &&
            dup2(pipe_fds[index][1], STDOUT_FILENO) < 0) {
            _exit(1);
        }
        close_pipeline_fds(pipe_fds, pipe_count);
        for (size_t cleanup = 0; cleanup < command_count; cleanup++) {
            if (cleanup != index) {
                input_redirection_close_parent(&inputs[cleanup]);
                output_redirection_close_parent(&outputs[cleanup]);
            }
        }
        if (input_redirection_connect_child(&inputs[index]) != 0 ||
            output_redirection_connect_child(&outputs[index]) != 0) {
            _exit(1);
        }

        const FlatCmd *command = &pipeline->commands[index];
        Token stage_tokens[MAX_ARGS];
        CommandLine stage_command =
            make_stage_command(command, stage_tokens);
        if (is_builtin_name(command->argv[0])) {
            _exit(builtin_execute(state, &stage_command) == BUILTIN_HANDLED
                      ? 0
                      : 1);
        }
        if (resolved_paths[index] == NULL) {
            fprintf(stderr, "cshell: command not found (%s)\n",
                    display_name(command->argv[0]));
            _exit(127);
        }
        char *argv[MAX_ARGS + 1];
        memcpy(argv, command->argv, (command->argc + 1) * sizeof(*argv));
        argv[0] = (char *)display_name(command->argv[0]);
        execve(resolved_paths[index], argv, environ);
        fprintf(stderr, "cshell: command not found (%s)\n",
                display_name(command->argv[0]));
        _exit(127);
    }

    close_pipeline_fds(pipe_fds, pipe_count);
    for (size_t index = 0; index < command_count; index++) {
        if (inputs[index].input_count != 0) {
            input_writers[index] = input_redirection_start_writer(&inputs[index]);
        }
        if (outputs[index].output_count != 0) {
            output_writers[index] =
                output_redirection_start_writer(&outputs[index]);
        }
    }

    for (size_t index = 0; index < command_count; index++) {
        (void)waitpid(children[index], NULL, 0);
    }
    for (size_t index = 0; index < command_count; index++) {
        if (input_writers[index] >= 0) {
            (void)input_redirection_wait_writer(input_writers[index]);
        }
        if (output_writers[index] >= 0) {
            (void)output_redirection_wait_writer(output_writers[index]);
        }
        free(resolved_paths[index]);
    }
    return EXEC_HANDLED;
}

ExecResult execute_part_c(const CommandLine *command_line, ShellState *state)
{
    if (command_line == NULL || state == NULL) {
        return EXEC_ERROR;
    }

    FlatCmd command;
    int parse_result = exec_parse_first(&command_line->tokens, &command);
    if (parse_result == 1 && command.has_pipeline) {
        Pipeline pipeline;
        if (exec_parse_pipeline(&command_line->tokens, &pipeline) != 0) {
            return EXEC_ERROR;
        }
        return execute_pipeline(&pipeline, state);
    }
    if (parse_result != 0 || command.argc == 0) {
        return EXEC_NOT_HANDLED;
    }

    InputRedirection input;
    int input_result = input_redirection_open(&command, &input);
    if (input_result == 1) {
        return EXEC_HANDLED;
    }
    if (input_result != 0) {
        fprintf(stderr, "cshell: unable to prepare input\n");
        return EXEC_ERROR;
    }

    OutputRedirection output;
    int output_result = output_redirection_open(&command, &output);
    if (output_result == 1) {
        input_redirection_close_parent(&input);
        return EXEC_HANDLED;
    }
    if (output_result != 0) {
        input_redirection_close_parent(&input);
        fprintf(stderr, "cshell: unable to prepare output\n");
        return EXEC_ERROR;
    }

    CommandLine first_command = {.tokens = {
        .items = command_line->tokens.items,
        .count = command.argc,
        .capacity = command.argc,
    }};
    BuiltinResult builtin = builtin_execute(state, &first_command);
    if (builtin != BUILTIN_NOT_FOUND && input.input_count == 0 &&
        output.output_count == 0) {
        return builtin == BUILTIN_HANDLED ? EXEC_HANDLED : EXEC_ERROR;
    }

    char *resolved_path = resolve_cmd_path(command.argv[0]);
    if (builtin == BUILTIN_NOT_FOUND && resolved_path == NULL) {
        input_redirection_close_parent(&input);
        output_redirection_close_parent(&output);
        fprintf(stderr, "cshell: command not found (%s)\n",
                display_name(command.argv[0]));
        return EXEC_HANDLED;
    }

    char *argv[MAX_ARGS + 1];
    memcpy(argv, command.argv, (command.argc + 1) * sizeof(*argv));
    argv[0] = (char *)display_name(command.argv[0]);

    pid_t child = fork();
    if (child < 0) {
        free(resolved_path);
        input_redirection_close_parent(&input);
        output_redirection_close_parent(&output);
        fprintf(stderr, "cshell: unable to fork\n");
        return EXEC_ERROR;
    }
    if (child == 0) {
        if (input_redirection_connect_child(&input) != 0) {
            _exit(1);
        }
        if (output_redirection_connect_child(&output) != 0) {
            _exit(1);
        }
        if (builtin != BUILTIN_NOT_FOUND) {
            _exit(builtin_execute(state, &first_command) == BUILTIN_HANDLED
                      ? 0
                      : 1);
        }
        execve(resolved_path, argv, environ);
        fprintf(stderr, "cshell: command not found (%s)\n",
                display_name(command.argv[0]));
        _exit(127);
    }

    free(resolved_path);
    int writer = -1;
    if (input.input_count != 0) {
        writer = input_redirection_start_writer(&input);
        if (writer < 0) {
            input_redirection_close_parent(&input);
            fprintf(stderr, "cshell: unable to stream input\n");
            (void)waitpid(child, NULL, 0);
            return EXEC_ERROR;
        }
    }
    int output_writer = -1;
    if (output.output_count != 0) {
        output_writer = output_redirection_start_writer(&output);
        if (output_writer < 0) {
            output_redirection_close_parent(&output);
            fprintf(stderr, "cshell: unable to stream output\n");
            (void)waitpid(child, NULL, 0);
            if (writer >= 0) {
                (void)input_redirection_wait_writer(writer);
            }
            return EXEC_ERROR;
        }
    }
    int status;
    if (waitpid(child, &status, 0) < 0) {
        if (writer >= 0) {
            (void)input_redirection_wait_writer(writer);
        }
        if (output_writer >= 0) {
            (void)output_redirection_wait_writer(output_writer);
        }
        fprintf(stderr, "cshell: wait failed\n");
        return EXEC_ERROR;
    }
    if (writer >= 0 && input_redirection_wait_writer(writer) != 0) {
        return EXEC_ERROR;
    }
    if (output_writer >= 0 &&
        output_redirection_wait_writer(output_writer) != 0) {
        return EXEC_ERROR;
    }
    return EXEC_HANDLED;
}
