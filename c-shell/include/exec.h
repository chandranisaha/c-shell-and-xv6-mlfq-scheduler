#ifndef CSHELL_EXEC_H
#define CSHELL_EXEC_H

#include "command.h"
#include "shell.h"

#define MAX_ARGS 128
#define MAX_REDIRS 32
#define MAX_PIPELINE 32

typedef struct {
    char *filename;
    TokenType type;
} RedirSpec;

typedef struct {
    char *argv[MAX_ARGS + 1];
    size_t argc;
    RedirSpec redirs[MAX_REDIRS];
    size_t redir_count;
    int has_pipeline;
} FlatCmd;

typedef struct {
    FlatCmd commands[MAX_PIPELINE];
    size_t count;
} Pipeline;

typedef struct {
    int *input_files;
    size_t input_count;
    int stream[2];
} InputRedirection;

typedef struct {
    int *output_files;
    size_t output_count;
    int stream[2];
} OutputRedirection;

typedef enum {
    EXEC_NOT_HANDLED,
    EXEC_HANDLED,
    EXEC_ERROR,
    EXEC_STOPPED
} ExecResult;

int exec_parse_first(const TokenList *tokens, FlatCmd *command);
int exec_parse_pipeline(const TokenList *tokens, Pipeline *pipeline);
char *resolve_cmd_path(const char *command_name);
int input_redirection_open(const FlatCmd *command, InputRedirection *input);
int input_redirection_connect_child(InputRedirection *input);
int input_redirection_start_writer(InputRedirection *input);
void input_redirection_close_parent(InputRedirection *input);
int input_redirection_wait_writer(int writer_pid);
int output_redirection_open(const FlatCmd *command, OutputRedirection *output);
int output_redirection_connect_child(OutputRedirection *output);
int output_redirection_start_writer(OutputRedirection *output);
void output_redirection_close_parent(OutputRedirection *output);
int output_redirection_wait_writer(int writer_pid);
ExecResult execute_part_c(const CommandLine *command_line, ShellState *state);
ExecResult execute_part_d(const CommandLine *command_line, ShellState *state);
ExecResult execute_part_d_background(const CommandLine *command_line,
                                     ShellState *state);

#endif
