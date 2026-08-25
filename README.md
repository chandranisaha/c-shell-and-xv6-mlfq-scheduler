# CS3.301 OSN Mini Project 1

## Student

- Name: Chandrani Saha
- Roll number: 2024113002

This repository contains the modular C-Shell implementation and the xv6
portion of the CS3.301 Operating Systems and Networks mini-project.

## C-Shell

The C-Shell is a small POSIX-oriented command interpreter written in C. It
reads one command line at a time, tokenizes and validates the complete line,
then dispatches the valid command to either a shell intrinsic or an external
executable. The shell maintains its own working-directory state, displays a
custom prompt, supports persistent directory frecency, and implements the
first four parts of command execution: external commands, file redirection,
and pipelines.

Parts A and B are complete. Part C1-C4 is implemented and tested in WSL.
Sequential execution, background jobs, process groups, terminal control,
later shell intrinsics, and the xv6 scheduler work are still pending.

Implemented shell features:

- A1: prompt with username, hostname, current path, and launch-directory `~`
  abbreviation.
- A2: interactive line input, repeated prompts, whitespace preservation, and
  EOF handling.
- A3: lexer and parser for words, quoting, escaping, pipes, redirections,
  semicolons, and background syntax validation.
- B1: `hop` with home, previous-directory, relative/absolute paths, and
  deterministic persistent frecency fallback.
- B2: `reveal` with `-a`, `-t`, combined flags, sorting, recursion, and
  directory resolution.
- B3: `peek` with `-n`, `-r`, multiple files, standard input, fixed-size
  reverse reads for regular files, and exact error handling.
- B4: `locate` with current-directory priority, ordered `PATH` search, all
  executable matches, absolute non-canonicalized paths, and missing-command
  reporting.
- C1: foreground execution of relative, absolute, PATH, and `%name` commands.
- C2: multiple input redirections concatenated in command-line order.
- C3: truncating/appending output redirection, multiple output targets, and
  input/output combinations.
- C4: multi-stage pipelines with one child per stage and failed-stage
  continuation.

## Architecture and design choices

The shell is split by responsibility rather than implemented as one monolithic
source file:

```text
main.c
  -> input.c
  -> lexer.c -> token.c
  -> parser.c -> command.c
  -> builtin.c
       -> hop.c -> path_utils.c -> frecency.c
       -> reveal.c
       -> peek.c
       -> locate.c
  -> exec.c -> exec_parse.c -> exec_resolver.c
                       -> exec_redir.c
```

Important implementation decisions:

1. `ShellState` stores the immutable launch directory as shell home and the
   previous directory used by `hop -` and `reveal -`.
2. Input is read with POSIX `read()` so `peek -` can consume standard input
   without conflicting with buffered stdio input.
3. The lexer performs maximal-munch tokenization and resolves quoting and
   escaping before parsing. The parser validates the complete line before any
   command executes.
4. Directory-changing built-ins run in the parent process. Built-ins used with
   redirection or inside a pipeline run in a child copy, because a child cannot
   change the parent shell's directory.
5. C1 resolves executable paths explicitly and invokes `execve()` directly;
   `system()` and `popen()` are not used.
6. C2 uses a feeder process to stream multiple input files in order. C3 uses a
   fan-out process so every output target receives the complete command output.
7. C4 creates one POSIX pipe per `|`, forks one child per pipeline stage, closes
   unused descriptors, applies explicit redirections after pipe connections,
   and waits for every stage and helper process.
8. The implementation is compiled with the assignment's C23/POSIX feature
   definitions and warning-as-error flags.

For the detailed design history, file index, corrections, and test evidence,
see [`DESIGN_LOG.md`](DESIGN_LOG.md). Assignment compliance checks are listed
in [`ASSIGNMENT_RULES.md`](ASSIGNMENT_RULES.md).

## Build and run in WSL

```bash
cd /mnt/c/Users/CHANDRANI/Downloads/osn-mp1/c-shell
make clean && make all
./shell.out
```

The required compiler flags are included in the Makefile. The executable is
created as `c-shell/shell.out`.

## Incremental development

The project is maintained as an incremental Git repository. Each milestone
has a focused commit, and generated build artifacts and unrelated later-part
files are kept out of milestone commits. The current history can be viewed
with:

```bash
git log --oneline --decorate
```

AI assistance and design decisions are documented separately as required by
the assignment.
