#include "exec.h"

#include "builtin.h"
#include "signals.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

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
            strcmp(visible_name, "locate") == 0 ||
            strcmp(visible_name, "activities") == 0 ||
            strcmp(visible_name, "resume") == 0 ||
            strcmp(visible_name, "ping") == 0);
}

/* Runs a builtin in a forked child and returns the exit status to hand to
 * _exit(). The fflush is the whole point: builtins print through stdio,
 * and stdio is fully buffered (not line buffered) whenever stdout is a
 * pipe or a file instead of a terminal. _exit() deliberately skips atexit
 * handlers and stream flushing, so without this every builtin that prints
 * via printf/fwrite - reveal, locate, peek -n - silently threw its whole
 * output away the moment it was redirected or put in a pipeline, while
 * peek's plain path survived only because it happens to use raw write().
 * fflush(NULL) covers stdout and any other open stream; a failure here
 * means the output never made it, so it has to be reported as failure. */
static int run_builtin_child(ShellState *state, const CommandLine *command)
{
    BuiltinResult result = builtin_execute(state, command);
    if (fflush(NULL) != 0) {
        return 1;
    }
    return result == BUILTIN_HANDLED ? 0 : 1;
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

/* Closes whichever ends of the Q47 starting-gun pipe are still open in
 * this process, idempotently. Closing the write end is what releases the
 * children, so every path out of execute_pipeline() has to reach this. */
static void close_sync_pipe(int sync_fds[2])
{
    for (size_t end = 0; end < 2; end++) {
        if (sync_fds[end] >= 0) {
            close(sync_fds[end]);
            sync_fds[end] = -1;
        }
    }
}

/* Child side of the starting gun: drop our copy of the write end (so the
 * parent's close can actually reach zero writers) and block until the
 * parent fires it. */
static void wait_for_start(int sync_fds[2])
{
    if (sync_fds[1] < 0) {
        return;
    }
    close(sync_fds[1]);
    sync_fds[1] = -1;
    char ignored;
    ssize_t amount;
    do {
        amount = read(sync_fds[0], &ignored, 1);
    } while (amount < 0 && errno == EINTR);
    close(sync_fds[0]);
    sync_fds[0] = -1;
}

/* Best-effort terminal handoff for E2: give/reclaim the controlling
 * terminal so job-control signals (SIGINT/SIGTSTP) reach whichever process
 * group is actually in the foreground. Silently does nothing when stdin
 * isn't a controlling terminal (e.g. redirected test fixtures). */
void give_terminal(const ShellState *state, pid_t pgid)
{
    if (state == NULL || state->terminal_fd < 0) {
        return;
    }
    (void)tcsetpgrp(state->terminal_fd, pgid);
}

static void append_argv_display(char *buffer, size_t buffer_size, size_t *used,
                                char *const argv[], size_t argc)
{
    for (size_t arg = 0; arg < argc; arg++) {
        bool need_space = *used > 0 && buffer[*used - 1] != ' ';
        int written = snprintf(buffer + *used, buffer_size - *used, "%s%s",
                               need_space ? " " : "", argv[arg]);
        if (written < 0 || (size_t)written >= buffer_size - *used) {
            *used = buffer_size - 1;
            return;
        }
        *used += (size_t)written;
    }
}

static void build_pipeline_display(const Pipeline *pipeline, char *buffer,
                                   size_t buffer_size)
{
    size_t used = 0;
    buffer[0] = '\0';
    for (size_t index = 0; index < pipeline->count; index++) {
        const FlatCmd *command = &pipeline->commands[index];
        append_argv_display(buffer, buffer_size, &used, command->argv,
                            command->argc);
        if (index + 1 < pipeline->count && used + 3 < buffer_size) {
            buffer[used++] = ' ';
            buffer[used++] = '|';
            buffer[used++] = ' ';
            buffer[used] = '\0';
        }
    }
}

static ExecResult execute_pipeline(const Pipeline *pipeline,
                                   ShellState *state, int background)
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

    /* Q47: the `[job] pid` line must appear before any of the background
     * command's own output, but fork() returns in parent and child at the
     * same instant, so without help the child can reach execve() first.
     * This pipe is the starting gun: every child blocks reading it and is
     * released only once the parent has closed the write end, which it
     * does immediately after printing the job line. read() returns 0 only
     * when *every* copy of the write end is gone, so one close in the
     * parent frees the whole pipeline at once. Foreground commands print
     * no job line and so need no starting gun. */
    int sync_fds[2] = {-1, -1};
    if (background && pipe(sync_fds) != 0) {
        close_pipeline_fds(pipe_fds, pipe_count);
        for (size_t cleanup = 0; cleanup < command_count; cleanup++) {
            input_redirection_close_parent(&inputs[cleanup]);
            output_redirection_close_parent(&outputs[cleanup]);
            free(resolved_paths[cleanup]);
        }
        fprintf(stderr, "cshell: unable to create pipe\n");
        return EXEC_ERROR;
    }

    size_t launched = 0;
    pid_t process_group = 0;
    for (size_t index = 0; index < command_count; index++) {
        pid_t child = fork();
        if (child < 0) {
            fprintf(stderr, "cshell: unable to fork\n");
            close_pipeline_fds(pipe_fds, pipe_count);
            /* Release any children already parked on the starting gun,
             * otherwise the waitpid() loop below waits on processes that
             * are themselves waiting on us. */
            close_sync_pipe(sync_fds);
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
            if (process_group == 0) {
                process_group = child;
            }
            if (setpgid(child, process_group) != 0 && errno != EACCES) {
                fprintf(stderr, "cshell: unable to create process group\n");
            }
            continue;
        }

        if (setpgid(0, process_group) != 0) {
            _exit(1);
        }
        signals_restore_terminal_defaults();
        wait_for_start(sync_fds);

        /* background jobs keep their stdin. pointing it at /dev/null kills
         * anything that reads - `cat | sort &` saw EOF and vanished before
         * activities could list it. the kernel already keeps them off the
         * terminal: a background group that reads it gets SIGTTIN. */

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
        if (command->argv[0][0] == '\0') {
            _exit(0);
        }
        if (is_builtin_name(command->argv[0])) {
            _exit(run_builtin_child(state, &stage_command));
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

    if (!background) {
        state->foreground_pgid = process_group;
        give_terminal(state, process_group);
    }
    close_pipeline_fds(pipe_fds, pipe_count);
    if (sync_fds[0] >= 0) {
        close(sync_fds[0]);
        sync_fds[0] = -1;
    }

    /* The job line and the starting gun both have to come before the
     * redirection helpers are forked: those helpers never exec, so a copy
     * of the write end left open in one of them would keep the children
     * parked for as long as the helper runs. */
    if (background) {
        char command_line[4096];
        build_pipeline_display(pipeline, command_line, sizeof(command_line));

        Job *job = job_create(state->next_job_number, process_group,
                              command_line);
        if (job == NULL) {
            close_sync_pipe(sync_fds);
            (void)kill(-process_group, SIGTERM);
            for (size_t index = 0; index < command_count; index++) {
                (void)waitpid(children[index], NULL, 0);
                free(resolved_paths[index]);
            }
            return EXEC_ERROR;
        }
        for (size_t index = 0; index < command_count; index++) {
            if (job_add_process(job, children[index],
                                pipeline->commands[index].argv[0]) != 0) {
                job_destroy(job);
                close_sync_pipe(sync_fds);
                (void)kill(-process_group, SIGTERM);
                for (size_t cleanup = 0; cleanup < command_count; cleanup++) {
                    (void)waitpid(children[cleanup], NULL, 0);
                    free(resolved_paths[cleanup]);
                }
                return EXEC_ERROR;
            }
        }
        job_add(state, job);
        printf("[%d] %ld\n", state->next_job_number,
               (long)children[0]);
        fflush(stdout);
        state->next_job_number++;
        /* Job line is on screen and flushed - fire the starting gun. */
        close_sync_pipe(sync_fds);
    }

    for (size_t index = 0; index < command_count; index++) {
        if (inputs[index].input_count != 0) {
            input_writers[index] = input_redirection_start_writer(&inputs[index]);
        }
        if (outputs[index].output_count != 0) {
            output_writers[index] =
                output_redirection_start_writer(&outputs[index]);
        }
    }

    if (background) {
        for (size_t index = 0; index < command_count; index++) {
            free(resolved_paths[index]);
        }
        return EXEC_HANDLED;
    }

    /* Scratch job used only if Ctrl-Z stops this pipeline; discarded
     * otherwise. Built before waiting so job_update_process() has
     * somewhere to record each process's status as it changes. */
    char display[4096];
    build_pipeline_display(pipeline, display, sizeof(display));
    Job *scratch = job_create(0, process_group, display);
    if (scratch != NULL) {
        for (size_t index = 0; index < command_count; index++) {
            if (job_add_process(scratch, children[index],
                                pipeline->commands[index].argv[0]) != 0) {
                job_destroy(scratch);
                scratch = NULL;
                break;
            }
        }
    }

    ExecResult pipeline_result = EXEC_HANDLED;
    bool any_stopped = false;
    bool any_signaled = false;
    for (size_t index = 0; index < command_count; index++) {
        int status;
        pid_t waited;
        do {
            waited = waitpid(children[index], &status, WUNTRACED);
        } while (waited < 0 && errno == EINTR);
        if (waited < 0) {
            pipeline_result = EXEC_ERROR;
            continue;
        }
        if (WIFEXITED(status) && WEXITSTATUS(status) == 127) {
            pipeline_result = EXEC_ERROR;
        }
        if (scratch != NULL) {
            job_update_process(scratch, children[index], status);
        }
        if (WIFSTOPPED(status)) {
            any_stopped = true;
        }
        if (WIFSIGNALED(status)) {
            any_signaled = true;
        }
    }

    give_terminal(state, state->shell_pgid);
    state->foreground_pgid = 0;

    if (any_stopped && scratch != NULL) {
        scratch->job_number = state->next_job_number++;
        job_add(state, scratch);
        /* Ctrl-Z is echoed by the tty as "^Z" with no newline after it, so
         * without this the Stopped line lands glued to it. The E2 and E3
         * examples both show them on separate lines. Same reasoning as the
         * WIFSIGNALED newline a few lines below; one newline for the whole
         * pipeline, not one per stopped stage. */
        putchar('\n');
        printf("[%d] + Stopped    %s\n", scratch->job_number,
               scratch->command_line);
        fflush(stdout);
        /* Any redirection helper writers are left running rather than
         * waited on here: they aren't part of the job's process group, so
         * they didn't stop with it, and one blocked writing into a pipe
         * the now-stopped job isn't reading would hang the shell. They are
         * still reaped generically by jobs_reap_background once they
         * eventually exit. */
        for (size_t index = 0; index < command_count; index++) {
            free(resolved_paths[index]);
        }
        return EXEC_STOPPED;
    }
    if (scratch != NULL) {
        job_destroy(scratch);
    }

    if (any_signaled) {
        /* A foreground process killed by an uncaught signal (Ctrl-C being
         * the common case) very likely left the cursor mid-line - nothing
         * else in this codebase checks whether the terminal is at column 0
         * before printing the next prompt, so make sure of it here. Not
         * specified anywhere in rules.md; matches real shells' own
         * behavior and keeps this consistent with execute_part_c(). */
        putchar('\n');
        fflush(stdout);
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
    return pipeline_result;
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
        return execute_pipeline(&pipeline, state, 0);
    }
    if (parse_result != 0 || command.argc == 0) {
        return EXEC_NOT_HANDLED;
    }
    if (command.argv[0][0] == '\0') {
        return EXEC_HANDLED;
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
    /* Only a name check here, not an actual call: builtin_execute() has
     * real side effects (peek reads stdin, hop changes the cwd, ...), and
     * with redirection present those must happen exactly once, in the
     * child, after the redirection is actually connected - never here in
     * the parent first. Calling it unconditionally used to run every
     * builtin combined with redirection twice, silently against the
     * shell's own real stdin/stdout the first time. */
    bool command_is_builtin = is_builtin_name(command.argv[0]);
    if (command_is_builtin && input.input_count == 0 &&
        output.output_count == 0) {
        BuiltinResult builtin = builtin_execute(state, &first_command);
        return builtin == BUILTIN_HANDLED ? EXEC_HANDLED : EXEC_ERROR;
    }

    char *resolved_path = NULL;
    if (!command_is_builtin) {
        resolved_path = resolve_cmd_path(command.argv[0]);
        if (resolved_path == NULL) {
            input_redirection_close_parent(&input);
            output_redirection_close_parent(&output);
            fprintf(stderr, "cshell: command not found (%s)\n",
                    display_name(command.argv[0]));
            return EXEC_ERROR;
        }
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
        if (setpgid(0, 0) != 0) {
            _exit(1);
        }
        signals_restore_terminal_defaults();
        if (input_redirection_connect_child(&input) != 0) {
            _exit(1);
        }
        if (output_redirection_connect_child(&output) != 0) {
            _exit(1);
        }
        if (command_is_builtin) {
            _exit(run_builtin_child(state, &first_command));
        }
        execve(resolved_path, argv, environ);
        fprintf(stderr, "cshell: command not found (%s)\n",
                display_name(command.argv[0]));
        _exit(127);
    }

    if (setpgid(child, child) != 0 && errno != EACCES) {
        fprintf(stderr, "cshell: unable to create process group\n");
    }
    state->foreground_pgid = child;
    give_terminal(state, child);

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
    pid_t waited;
    do {
        waited = waitpid(child, &status, WUNTRACED);
    } while (waited < 0 && errno == EINTR);
    if (waited < 0) {
        if (writer >= 0) {
            (void)input_redirection_wait_writer(writer);
        }
        if (output_writer >= 0) {
            (void)output_redirection_wait_writer(output_writer);
        }
        give_terminal(state, state->shell_pgid);
        state->foreground_pgid = 0;
        fprintf(stderr, "cshell: wait failed\n");
        return EXEC_ERROR;
    }

    give_terminal(state, state->shell_pgid);
    state->foreground_pgid = 0;

    if (WIFSTOPPED(status)) {
        char display[4096];
        size_t used = 0;
        display[0] = '\0';
        append_argv_display(display, sizeof(display), &used, command.argv,
                            command.argc);
        int job_number = state->next_job_number;
        Job *job = job_create(job_number, child, display);
        if (job != NULL && job_add_process(job, child, command.argv[0]) != 0) {
            job_destroy(job);
            job = NULL;
        }
        if (job != NULL) {
            job_update_process(job, child, status);
            job_add(state, job);
            state->next_job_number++;
            /* See execute_pipeline(): move past the tty's echoed "^Z". */
            putchar('\n');
            printf("[%d] + Stopped    %s\n", job_number, display);
            fflush(stdout);
        }
        /* See execute_pipeline()'s stopped path: redirection helper
         * writers are deliberately left running, not waited on here. */
        return EXEC_STOPPED;
    }

    if (WIFSIGNALED(status)) {
        /* See execute_pipeline()'s matching check: an uncaught signal
         * (Ctrl-C) very likely left the cursor mid-line, and nothing
         * ensures the next prompt starts on a fresh one otherwise. */
        putchar('\n');
        fflush(stdout);
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

ExecResult execute_part_d_background(const CommandLine *command_line,
                                     ShellState *state)
{
    if (command_line == NULL || state == NULL) {
        return EXEC_ERROR;
    }
    Pipeline pipeline;
    if (exec_parse_pipeline(&command_line->tokens, &pipeline) != 0) {
        return EXEC_ERROR;
    }
    return execute_pipeline(&pipeline, state, 1);
}
