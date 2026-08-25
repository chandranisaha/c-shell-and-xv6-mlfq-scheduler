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

ExecResult execute_part_c(const CommandLine *command_line, ShellState *state)
{
    if (command_line == NULL || state == NULL) {
        return EXEC_ERROR;
    }

    FlatCmd command;
    int parse_result = exec_parse_first(&command_line->tokens, &command);
    if (parse_result != 0 || command.argc == 0 || command.has_pipeline) {
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
