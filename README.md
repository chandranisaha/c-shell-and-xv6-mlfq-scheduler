# CS3.301 OSN Mini Project 1

Name: Chandrani Saha
Roll number: 2024113002

This is my mid-submission for the CS3.301 Operating Systems and Networks
mini-project. It contains my C-Shell implementation completed through Part C.

## C-Shell

I implemented the C-Shell as a small POSIX-oriented command interpreter in C.
It reads one command line at a time, tokenizes and validates the complete line,
then dispatches valid commands to shell intrinsics or external executables.
The shell maintains its own working-directory state, displays a custom prompt,
supports persistent directory frecency, and implements command execution,
redirection, and pipelines.

Parts A, B, C, and D are complete and tested in WSL. Part E1 (`activities`)
and Part E2 (terminal control) are complete. `resume`/`ping` (E3/E4) and the
xv6 scheduler work are still pending.

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
- D1: sequential execution (`command1 ; command2 ; ... ; commandN`) executed
  in order, waiting on each stage, and halting on unresolvable commands.
- D2: background execution (`command1 & command2 & ... & commandN &`) with
  session-wide monotonic job numbering, child process groups, terminal isolation,
  SIGCHLD reaping via `waitpid(WNOHANG)`, and normal/abnormal exit status reporting.
  A command trailing the last `&` with no `&` of its own runs in the
  foreground, per the A3 grammar (`BG -> WORD ARG`, no operator required) and
  the assignment's own `sleep 1 & sleep 2 & cat` example. Background
  completions are reported as soon as they happen, including mid-read at the
  prompt, without disturbing text the user has already typed on the current
  line (see `DESIGN_LOG.md` entry d2-010 for the interactive-echo bug this
  replaced).
- E1: `activities` lists every tracked process group, oldest first, one
  `[job_number] pgid <pgid>` line followed by an indented `<pid> <name>
  <state>` line per still-running process, reaping already-exited processes
  before printing so finished ones never show up.
- E2 (Ctrl-C/Ctrl-Z): the shell ignores `SIGINT`/`SIGTSTP`/`SIGTTOU` for
  itself and hands the controlling terminal to a foreground job's process
  group via `tcsetpgrp()` before waiting on it (reclaiming it afterward), so
  Ctrl-C interrupts only the foreground job and Ctrl-Z stops it with
  `waitpid(..., WUNTRACED)`, printing `[job_number] + Stopped    <command>`
  and registering it as a tracked job (visible in `activities`) before
  returning to the prompt. Background jobs are never handed the terminal, so
  they're unaffected by either. Stopping a command partway through a `;`
  sequence halts the rest of that sequence, the same as an unresolved
  command does.
- E2 (Ctrl-D/exit): Ctrl-D at the prompt exits, unless a job is currently
  Stopped, in which case it prints `cshell: there are stopped jobs` and
  returns to the prompt instead — pressing Ctrl-D again right away (no other
  input typed in between) exits anyway. Whenever the shell exits with any
  background or stopped job still tracked, every one of them gets `SIGHUP`
  sent to its process group first, without waiting for them to react.

## Architecture and design choices

The shell is split by responsibility rather than implemented as one monolithic
source file:

```text
main.c
  -> input.c
  -> lexer.c -> token.c
  -> parser.c -> command.c
  -> sequence.c
  -> builtin.c
       -> hop.c -> path_utils.c -> frecency.c
       -> reveal.c
       -> peek.c
       -> locate.c
       -> activities.c -> jobs.c
  -> exec.c -> exec_parse.c -> exec_resolver.c
                       -> exec_redir.c
  -> jobs.c
  -> signals.c
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
9. `signals.c` installs a minimal `SIGCHLD` handler that only sets a
   `volatile sig_atomic_t` flag (no `printf`/`malloc` inside the handler).
   `jobs.c` does the real reaping (`waitpid(-1, ..., WNOHANG)` in a loop)
   from normal shell code: once at the top of the main loop before each
   prompt, and from `input_read_line`'s `EINTR` branch so a background job
   finishing while the shell is blocked reading the next line is reported
   immediately rather than only after the next command runs. The terminal is
   left in canonical mode (no `tcsetattr`), so that `EINTR` handler never
   erases or rewrites anything already echoed by the kernel for the
   in-progress line — it only adds a leading newline and prints the
   notification plus a fresh prompt underneath.
10. `activities` is dispatched through the same `builtin_execute`/
    `is_builtin_name` path as `hop`/`reveal`/`peek`/`locate`, so it also works
    combined with redirection or inside a pipeline (which run it in a forked
    child — harmless here since it only reads job state, never mutates it).
    It reuses `jobs_reap_background` to drop already-exited processes before
    printing, and relies on the job list already being maintained in launch
    order (`job_add` appends at the tail) to print oldest group first.
11. E2 pairs `signals_ignore_terminal()` (shell process, at startup) with
    `signals_restore_terminal_defaults()` (every forked child, right after
    `setpgid()`, before it execve()s or runs a builtin). This second call is
    load-bearing, not decorative: `fork()` inherits the parent's `SIG_IGN`
    disposition, and `execve()` does not reset an already-ignored signal
    back to default, so without it every foreground job would silently
    inherit the shell's own ignore-disposition and become permanently immune
    to Ctrl-C/Ctrl-Z. Redirection helper (feeder/fan-out) processes are
    deliberately left un-waited-on if the job they serve gets stopped, since
    they aren't part of the job's process group and a writer blocked on a
    full pipe would otherwise hang the shell; they're still reaped without
    leaking zombies by the existing generic `jobs_reap_background` sweep.
12. The `SIGHUP`-on-exit broadcast lives inside `shell_state_destroy()`
    itself rather than at each of `main.c`'s several exit points, since that
    function was already the one thing called on every exit path — one
    place to get right instead of several places to remember. Note that a
    *stopped* (not just backgrounded) job doesn't necessarily die from this
    immediately: POSIX only wakes a stopped process for `SIGCONT`/`SIGKILL`,
    so `SIGHUP` alone can leave it parked until something continues it —
    `rules.md` only requires sending the signal without waiting, not
    guaranteeing termination, so no extra `SIGCONT` was added.
13. **Bug fix:** `execute_part_c` used to call `builtin_execute()`
    unconditionally in the parent before checking whether redirection was
    present, then — if redirection *was* present — also ran it a second time
    correctly redirected in the forked child. Every builtin combined with
    redirection ran twice; for `peek < file` with no filename argument this
    meant the first (erroneous) call read from the shell's own real stdin
    before the file was ever connected, with visibly wrong output. Fixed by
    splitting "is this a builtin" (a pure name check, no side effects) from
    actually calling `builtin_execute()`, which now happens in exactly one
    place depending on whether redirection is present. See `DESIGN_LOG.md`
    (`bug-001`) for the full diagnosis — caught by finally reading fixture
    *output*, not just exit codes.


## Build and run in WSL

```bash
cd /mnt/c/Users/CHANDRANI/Downloads/mini-project1/c-shell
make clean && make all
./shell.out
```

Ctrl+D to quit the shell

The required compiler flags are included in the Makefile. The executable is
created as `c-shell/shell.out`.
