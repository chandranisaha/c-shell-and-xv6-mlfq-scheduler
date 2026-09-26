# C-Shell and xv6 MLFQ Scheduler

Two systems programming projects built by Chandrani Saha: a Unix shell written from scratch in C, and a multi-level feedback queue scheduler added to the xv6 teaching kernel.

What's inside:

- **`c-shell/`**, a POSIX shell written from scratch in C, with its own lexer and parser, custom built-ins, redirection and pipes, sequential and background execution, job control, and the `spy` and `snoop` process tools (an `lsof`-style file lister and an `strace`-style syscall counter).
- **`xv6/`**, xv6-riscv with a multi-level feedback queue scheduler, a FIFO scheduler for comparison, a `waitx` system call, a `schedulertest` workload, a plotting script, and the scheduler report (`xv6/report.pdf`).

## Repository structure

```text
cshell-and-xv6-mlfq/
├── c-shell/
│   ├── src/            shell source files
│   ├── include/        headers
│   └── Makefile        make all builds shell.out
├── xv6/
│   ├── kernel/         scheduler, waitx and procdump changes
│   ├── user/           schedulertest and the waitx user stub
│   ├── mkfs/
│   ├── Makefile        SCHEDULER, TRACE and CPUS build options
│   ├── plot_mlfq.py    plotting code for the report figures
│   ├── trace.txt       MLFQ trace used for the timeline figure
│   └── report.pdf      implementation summary, MLFQ analysis, comparison
└── README.md
```

The rest of `xv6/` (`LICENSE`, `README`, `test-xv6.py` and the dotfiles) comes unchanged from upstream xv6.

## C-Shell

The C-Shell is a small interactive shell I wrote from scratch in C, using only
POSIX headers and functions. It reads one line at a time, tokenizes it and
checks it against a small shell grammar, and only then runs it. It
supports its own set of intrinsics (`hop`, `reveal`, `peek`, `locate`,
`activities`, `resume`, `ping`, `spy`, `snoop`), runs external programs with
`execve()`, and handles redirection, pipes, sequential and background
execution, and job control.

### Build and run

The shell is meant for Linux (I developed and ran it in WSL).

```bash
cd c-shell
make all        # builds shell.out in c-shell/
./shell.out
```

Press Ctrl-D at an empty prompt to exit. `make clean` removes the `build/`
directory and `shell.out`.

The required compiler flags are already in the Makefile:
`-std=c23 -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 -Wall -Wextra -Werror -Wno-unused-parameter -fno-asm`.

### Code layout

Every `src/*.c` file has a matching header in `include/` (the `exec_*.c`
files all share `exec.h`, and `builtin.h` declares the intrinsic entry points).

| File | Responsibility |
|---|---|
| `main.c` | Main loop: reap background jobs, print prompt, read, lex, validate, execute |
| `shell.c` | `ShellState` setup (launch directory, user, host, job list) and teardown, including SIGHUP on exit |
| `prompt.c` | Builds and prints `<user@host:path>` with the `~` substitution |
| `input.c` | Reads a line with `read()`, handles EOF, Ctrl-C at the prompt and the 1024 character limit |
| `token.c`, `command.c` | Token list storage and the `CommandLine` wrapper |
| `lexer.c` | Character level scanner: operators, words, quotes, escapes |
| `parser.c` | Recursive checker for the right linear grammar |
| `sequence.c` | Splits a valid line on `;` and `&` and dispatches each part |
| `exec.c` | Foreground commands, pipelines, background launch, terminal handoff, Ctrl-Z handling |
| `exec_parse.c` | Turns tokens into argv arrays and redirection lists, one per pipeline stage |
| `exec_resolver.c` | Finds the executable for a command name (`/` paths, current directory, `PATH`, `%name`) |
| `exec_redir.c` | Opens redirection files and runs the input feeder and output fan-out helpers |
| `builtin.c` | Dispatches intrinsic names to their implementations |
| `hop.c`, `path_utils.c`, `frecency.c` | `hop`, shared `~`/`-`/path resolution, persistent frecency store |
| `reveal.c`, `peek.c`, `locate.c` | `reveal`, `peek` and `locate` |
| `jobs.c` | Job and process tracking, SIGCHLD reaping, completion messages |
| `signals.c` | Flag-only handlers for SIGCHLD, SIGINT and SIGALRM, plus ignore/restore of terminal signals |
| `activities.c`, `resume.c`, `ping.c` | Job control intrinsics |
| `spy.c`, `snoop.c` | Process inspection tools |

### Features

#### Input and parsing

**Prompt.** The prompt is `<username@hostname:path> `. The directory the
shell is started in is its home. If the working directory is that directory
or below it, the prefix is replaced by `~` (so `~` or `~/src`); otherwise the
absolute path is shown as is. The prompt is printed only when no foreground
job is running.

**Input.** Input is read with plain `read()` on stdin rather than stdio,
so that `peek` can read the same stdin later without stdio having buffered it
away. A line longer than 1024 characters is thrown away and reported as
`cshell: invalid syntax`. An empty or all-whitespace line just gives a new
prompt.

**Lexer and parser.** The lexer follows maximal munch, so `>>` is one token and `hello|hi` is three.
Adjacent fragments join into one word (`abc"123"'def'` is `abc123def`, and
`""` is a valid empty word). Quoting rules:

- `\c` outside quotes gives `c`.
- Inside `"..."`, `\"` gives `"` and `\\` gives `\`; any other `\c` keeps both characters.
- Inside `'...'` everything is literal.
- Operators inside quotes or after a backslash lose their meaning.

An unclosed quote or a trailing `\` is a lexical error. The parser then checks
the token list against the grammar (`LINE`, `ARG`, `CMD`, `TGT`, `BG`) with one
small function per non-terminal. Nothing runs unless the whole line is valid;
otherwise the shell prints `cshell: invalid syntax`. So `echo hi ;`,
`echo hi & &`, `cat <`, `| sort` and `cmd & ;` are all rejected, while
`echo hi &` and `sleep 1 & echo done` are accepted.

#### Built-in commands

**Hop.** `hop` with no arguments or `~` goes to the home directory, `.`
does nothing, `..` goes to the parent, `-` goes to the previous directory (or
does nothing if there is none), and anything else is tried as a relative or
absolute path (`~/sub` works too). Arguments are processed left to right, each
as its own hop. If a bare name (no `/`, and not starting with `.`, `~` or `-`)
does not resolve, `hop` falls back to frecency: it picks the highest ranked
recorded directory whose path contains the name as a substring and still
exists on disk. If nothing matches it prints `hop: no such directory`.

Frecency is stored in `.cshell_frecency` inside the launch directory, one
`path<TAB>score<TAB>last_visit` line per directory. Each successful hop into a
new directory adds 1 to its score and updates the visit time. The rank is
`score / (1 + days_since_last_visit)`, with ties broken by the more recent
visit and then by path order, so the result is deterministic.

**Reveal.** `reveal (-(a|t)*)* (~ | . | .. | - | name)?` lists a directory
one entry per line, sorted by `strcmp` on the bare name. `-a` includes hidden
entries but never `.` and `..` (like `ls -A`). `-t` recurses: each
subdirectory is printed with a trailing `/` and its contents follow it with the
relative prefix, like `tree`. Names with spaces are wrapped in single
quotes. Symlinks to directories are listed but not followed. Flags must come
before the path; an unknown flag, a flag after the path, or more than one path
gives `reveal: invalid syntax`. A path that does not resolve, or `reveal -`
before any hop, gives `reveal: no such directory`.

**Peek.** `peek` prints files in order, or stdin when there are no file
arguments or the argument is `-`.

- `-n` numbers non-empty lines only, and the count continues across files. Empty lines are still printed, without a number.
- `-r` reverses the lines of each file separately, keeping the file order. For regular files it uses `lseek()` and reads backwards in 4096 byte chunks; pipes and stdin are buffered first.
- With `-nr`, each number stays attached to its original line, so two two-line files print `2, 1, 4, 3`.

Errors are `peek: no such file or directory` and `peek: is a directory`, and
processing continues with the next argument. An unknown flag (which also
covers file names starting with `-`) gives
`peek: invalid syntax`.

**Locate.** `locate name+` prints the absolute path of every executable
that a POSIX shell would find for each name: first the current directory, then
each `PATH` directory in order, without recursing. A match must be a regular
file the current user can execute. Symlinks are printed as found, not
resolved. A name with no match prints `locate: command not found (name)` and
the remaining names are still processed. With no arguments it prints
`locate: invalid syntax`.

#### Execution, redirection and pipes

**Command execution.** A name containing `/` is run as a literal path.
Otherwise the current directory is checked first, then `PATH`. `%name` skips
the current directory and searches only `PATH` (the program still sees `name`
as `argv[0]`). If nothing is found the shell prints
`cshell: command not found (name)`. Programs run with `fork()` and `execve()`;
`system()` and `popen()` are never used.

**Input redirection.** Every `<` file is opened with `O_RDONLY` before
anything is forked. If one fails, the shell prints
`cshell: no such file or directory` and the command does not run. With several
input files, a small feeder process writes them one after another into a pipe,
and the command gets the read end on `STDIN_FILENO` through `dup2()`, so it sees
one continuous stream.

**Output redirection.** Each `>` opens with `O_TRUNC` and each `>>` with
`O_APPEND`, both with `O_CREAT` and mode `0644`. If any file cannot be opened
the shell prints `cshell: unable to create file for writing` and does not run
the command. The command writes into a pipe, and a fan-out process copies
everything to every listed file, so `echo hi > a > b` fills both. Nothing
reaches the terminal.

**Pipes.** One `pipe()` per `|` and one child per stage. Each child
connects its pipe ends, closes every pipe descriptor it does not need, then
applies its own redirections, so `cmd > file | next` sends the output to the
file and `next` sees EOF. The parent closes all pipe ends and waits for every
stage. A stage that cannot be found prints `cshell: command not found (name)`
from its own child, and the other stages still run. Intrinsics work in
pipelines and with redirection too; they run in a forked child in that case.

#### Sequential and background execution

**Sequential.** A line is split on `;` and each part (which can be a whole
pipeline with redirections) runs to completion before the next one starts. The
sequence stops early only when a command could not be executed
(`command not found`) or when a foreground job is stopped with Ctrl-Z. A
command that runs and exits with a non-zero status does not stop it.

**Background.** Each part that ends in `&` is launched as its own job
without waiting, and a whole pipeline before `&` goes to the background
together. The shell prints `[job_number] pid`, where pid is the first process
of the pipeline. A trailing part with no `&` (as in `sleep 1 & sleep 2 & cat`)
runs in the foreground, like bash.
When a background job finishes, the shell prints
`name with pid N exited normally` if the first process exited on its own (any
exit code), or `exited abnormally` if a signal killed it. For a pipeline this
message is printed once, for the first command, after the whole group is done.

#### Job control

**Activities.** Every job runs in its own process group: `setpgid()` is
called in the parent right after `fork()` and again in the child. `activities`
first reaps anything that has changed state, then prints the jobs oldest first:

```text
[1] pgid 4021
  4021 sleep Running
[2] pgid 4030
  4030 cat Running
  4031 sort Running
```

Processes that already exited are left out. The Running/Stopped state is kept
current because the reaper also collects `WUNTRACED` and `WCONTINUED` events.

**Terminal control.**

- The shell installs a flag-only handler for SIGINT (so it never dies from Ctrl-C) and ignores SIGTSTP and SIGTTOU. Every child puts these back to their defaults before `execve()`, since an ignored disposition would otherwise survive `exec` and make the job immune to Ctrl-C and Ctrl-Z.
- Before waiting on a foreground job the shell hands it the terminal with `tcsetpgrp()`, and takes it back once the job finishes or stops. Background jobs never get the terminal.
- Ctrl-C kills only the foreground job. The shell then prints a newline so the next prompt starts on a clean line. Ctrl-C at an empty prompt just gives a fresh prompt.
- Ctrl-Z stops the foreground job (seen through `waitpid(..., WUNTRACED)`). The shell adds it to the job list and prints `[job_number] + Stopped command`.
- Ctrl-D on an empty prompt exits. If a job is Stopped, the shell prints `cshell: there are stopped jobs` and stays. A second Ctrl-D straight after that exits anyway. Ctrl-D on a half-typed line only flushes the text to `read()`, like bash, so the shell exits on the next Ctrl-D.
- On any exit, the shell sends SIGHUP to the process group of every tracked job and does not wait for them.

**Resume.** The syntax is `resume %N bg` or `resume %N fg [--timeout S]`,
where S is a non-negative integer. Anything else gives
`resume: invalid syntax`, and an unknown job gives `resume: no such job`. In
both modes the shell sends SIGCONT to the job's group and marks it Running.

- `bg` prints `[N] + Running    command` and returns at once.
- `fg` prints the command line, gives the job the terminal and waits. If the job stops again, the shell prints the Stopped line with the same job number. If it finishes, it is quietly removed.
- With `--timeout S`, the shell calls `alarm(S)` before waiting. If the alarm fires first, it sends SIGTERM to the group, prints `resume: job timed out`, takes the terminal back and removes the job. If the job ends or stops before that, the alarm is cancelled with `alarm(0)`. `--timeout 0` sends SIGTERM straight away.

**Ping.** `ping <target> <signal_number>` checks the signal number first:
anything that is not a non-negative integer (including negatives) prints
`ping: invalid syntax`, even if the target is also bad. A plain number is a
pid and gets the signal on its own; `%N` is a job and the whole group gets it.
The signal actually sent is `signal_number % 64`. On success the shell prints
`Sent signal <typed number> to <typed target>`, echoing exactly what was typed.
A target that is not a process or job this shell is tracking prints
`ping: no such process found`, even if that pid exists on the system. When
the signal is SIGSTOP or SIGCONT, the job's state is updated immediately so
`activities` shows it right away.

#### spy and snoop

**Spy.** `spy [pid]` lists open files by reading `/proc`, in this order:

```text
PID    FD    TYPE   PATH
812    cwd   DIR    /home/chandrani
812    txt   REG    /usr/bin/sleep
812    mem   REG    /usr/lib/x86_64-linux-gnu/libc.so.6
812    0     CHR    /dev/pts/0
```

`cwd` and `txt` come from `readlink()` on `/proc/pid/cwd` and `/proc/pid/exe`.
`mem` rows come from `/proc/pid/maps`, one row per unique file (the executable
is left out because it is already the `txt` row). Numeric descriptors come from
`/proc/pid/fd` in ascending order. TYPE (`REG`, `DIR`, `CHR`, `BLK`, `FIFO`,
`LNK`, `SOCK`) comes from `stat()` on the `/proc` entry itself, which is why a
pipe shows up as `FIFO` even though its "path" is `pipe:[12345]`. With no
argument it reports the shell, using the pid saved at startup, so the answer
is still the shell's even when `spy` runs in a child because of a pipe or
redirection. Errors: `spy: no such process`, and `spy: invalid syntax` for
more than one argument or a non-numeric one.

**Snoop.** `snoop command [args...]` forks, calls `PTRACE_TRACEME` in the
child and runs the command with `execve()`. `snoop -p pid` uses
`PTRACE_ATTACH`. The tracer then loops on `PTRACE_SYSCALL` with
`PTRACE_O_TRACESYSGOOD` set, reads the syscall number from `orig_rax` at each
entry stop, and measures the time from entry to exit with `CLOCK_MONOTONIC`.
When the tracee exits, it prints a summary sorted by call count, with ties in
order of first occurrence:

```text
syscall         calls   time
nanosleep       1       1.000s
write           1       0.000s
exit_group      1       0.000s
```

Numbers missing from my name table print as `syscall_N`. Calls are counted at
entry, so `exit_group`, which never returns, still shows up once with 0.000s,
as `strace -c` does. Errors are `snoop: command not found`, `snoop: no such process`
and `snoop: invalid syntax`.

### Assumptions and design decisions

- **Home is the launch directory.** `~` means the directory `shell.out` was started from, not `$HOME`. Only the prompt, `hop` and `reveal` understand `~`; the lexer does not expand it, so external commands receive it literally.
- **Frecency file.** The store is `.cshell_frecency` in the launch directory. So history carries over whenever the shell is launched from the same folder. It is rewritten after every recorded hop. The previous directory for `hop -` is not saved between sessions.
- **When frecency is used.** The fallback only applies to bare names. Something like `foo/bar` or `../x` either resolves as a path or fails. A hop is recorded only when the directory actually changes, so `hop .` and hopping to the directory you are already in do not count.
- **Where messages go.** Errors from command lookup, redirection, `hop`, `reveal`, `peek`, `spy` and `snoop` go to stderr. `cshell: invalid syntax`, `cshell: there are stopped jobs`, the `locate`, `resume` and `ping` messages, and job notices go to stdout.
- **When completion messages appear.** The SIGCHLD handler only sets a flag. The actual `waitpid(-1, ..., WNOHANG)` loop runs in the main loop right before the prompt is drawn, the way bash does it. This way a message never lands in the middle of a line the user is typing. The trade-off is that at an idle prompt you see the message after pressing Enter. Completions that happen while a foreground job runs are reported after it finishes.
- **Job line comes first.** Background children wait on a small sync pipe until the parent has printed and flushed `[N] pid`, so that line always comes before the job's own output.
- **Background jobs keep terminal stdin.** I do not redirect a background job's stdin to `/dev/null`. It is in its own process group and never receives the terminal, so if it tries to read, the kernel stops it with SIGTTIN before it gets any input. This keeps jobs like `cat | sort &` alive, which `activities` and `ping` rely on.
- **SIGTTIN and SIGTTOU stops show as Running.** A job stopped by the terminal in this way is still listed as `Running` in `activities`, which is how a `cat | sort &` job should look. It also does not count as a stopped job for the Ctrl-D check. Only real stops (Ctrl-Z, SIGSTOP through `ping`) show as `Stopped`.
- **Job numbers keep increasing.** One counter per run of the shell, shared by background launches and jobs created by Ctrl-Z. Numbers are never reused.
- **Intrinsics with pipes or redirection run in a child.** This follows bash, so `hop x | cat` does not change the shell's directory. The child flushes stdio before `_exit()`, because output to a pipe or file is fully buffered and would otherwise be lost.
- **Ctrl-Z is ignored during snoop.** The traced command stays in the shell's own foreground process group and is not a job, so a stopped tracee would leave the shell stuck waiting inside `snoop` with no way to resume it. For that reason the tracer swallows SIGTSTP. Other signals, including SIGINT, are passed on to the tracee, so Ctrl-C still kills a snooped command. With `-p`, Ctrl-C detaches from the process and prints what was collected so far.
- **snoop -p and ptrace_scope.** On WSL and most Ubuntu setups `/proc/sys/kernel/yama/ptrace_scope` is 1, which only allows attaching to your own descendants. So `snoop -p` works on jobs this shell started (for example `sleep 100 &`), but not on unrelated processes. In that case it prints `snoop: cannot attach to process`, which is separate from `snoop: no such process`.
- **spy on other users' processes.** For those processes `/proc` hides the links, so rather than print an empty table I print `spy: permission denied`.
- **Very long input.** Lines over 1024 characters are rejected as invalid syntax instead of being cut short.
- **Stopped jobs on exit.** The shell sends SIGHUP but not SIGCONT, and does not wait for them. A stopped job may therefore stay stopped until something continues it.

### Known limitations

- Line editing is whatever the terminal's canonical mode offers: no history, no tab completion, no arrow keys.
- There are fixed limits of 128 arguments per command, 32 redirections per command and 32 stages per pipeline.
- `hop a missing b` prints the error for `missing` but still tries `b`, instead of stopping at the last valid hop.
- If any stage of a pipeline has a redirection file that cannot be opened, the whole pipeline is not started. Output files are opened left to right, so ones before the failing file may already be created or truncated.
- A background command that does not exist still takes a job number, prints its `[N] pid` line and the not-found error, and is later reported as `exited normally` (exit status 127).
- Inside a foreground pipeline, a stage that really exits with status 127 is treated like "command not found" and stops a `;` sequence.
- `resume ... fg --timeout` sends SIGTERM only once. A job that ignores SIGTERM keeps the shell waiting until it exits or is interrupted with Ctrl-C.
- `resume` and `ping` only make sense in the shell process itself. In a pipeline or with redirection they run in a child that cannot wait on or update the shell's jobs.
- `snoop` reads registers with `user_regs_struct` and `orig_rax`, so it only works on x86-64. Its times include ptrace overhead, and it follows only the main traced process, not any children it forks.
- Error streams are not fully consistent: some intrinsic errors still go to stdout, as listed above.

## xv6 Scheduler

`xv6/` is MIT's xv6-riscv with a Multi-Level Feedback Queue (MLFQ) scheduler added next to the stock round robin one. It is a recent xv6 revision, so a few names differ from older write-ups: the kernel prints with `printk()`, exit is `kexit()`, fork is `kfork()`, and the sleep syscall is `pause()`.

What I added:

- **MLFQ**, selected with `SCHEDULER=MLFQ`: four queues, time slices of 1, 4, 8 and 16 ticks, preemption at tick boundaries and a priority boost every 48 ticks.
- **FIFO**, selected with `SCHEDULER=FIFO`, so there is a third policy for the comparison.
- **Round robin** stays the default. The policy is untouched; it only gained timing bookkeeping.
- **`waitx`**, a syscall that reaps a child and returns its turnaround, waiting, response and running time.
- **`schedulertest`**, a user program that forks children with different CPU burst lengths and reports their stats.
- **`plot_mlfq.py`**, which draws the queue timeline and the comparison charts.
- **`report.pdf`**, the report with the implementation summary, the plots and the analysis.

### Toolchain and build (WSL)

```bash
sudo apt install gcc-riscv64-unknown-elf qemu-system-riscv
cd xv6
```

The package really is `qemu-system-riscv`. Older xv6 instructions say `qemu-system-misc`, but recent Ubuntu releases moved `qemu-system-riscv64` into its own package.

Pick the policy when you build:

```bash
make clean && make qemu                          # round robin (default)
make clean && make qemu SCHEDULER=RR             # same thing, spelled out
make clean && make qemu SCHEDULER=MLFQ           # multi-level feedback queue
make clean && make qemu SCHEDULER=FIFO           # first come first served
make clean && make qemu SCHEDULER=MLFQ TRACE=1   # MLFQ plus the per-tick trace
make clean && make qemu SCHEDULER=MLFQ CPUS=1    # any of the above on one cpu
```

- `make clean` between policies is required. The `-D$(SCHEDULER)` flag is not a make dependency, so without a clean the old object files get reused and you silently run the previous scheduler.
- The Makefile only accepts `MLFQ`, `FIFO` or `RR`. Anything else stops the build with `SCHEDULER must be MLFQ, FIFO or RR`. `RR` and an unset `SCHEDULER` both compile with no scheduler macro, which is the original code path.
- `CPUS` defaults to 3. All the measurements below use `CPUS=1`.
- Inside QEMU, Ctrl-P prints the process table and Ctrl-A then X quits.
- Do not press the arrow keys at the xv6 prompt. The console has no line editing, so the escape sequences end up as junk in the command.

### MLFQ implementation

**SCHEDULER macro (Makefile).** `SCHEDULER=MLFQ` or `SCHEDULER=FIFO` adds `-DMLFQ` or `-DFIFO` to `CFLAGS`, and `proc.c` and `trap.c` pick their code with `#ifdef MLFQ` / `#elif defined(FIFO)` / `#else`. `TRACE=1` adds `-DMLFQTRACE`. `NQUEUE` (4) and `BOOST_INTERVAL` (48) live in `kernel/param.h`.

**struct proc changes (`kernel/proc.h`).** All of these are protected by `p->lock`, same as `state`:

| Field | Meaning |
|---|---|
| `queue` | MLFQ priority, 0 (highest) to 3 (lowest) |
| `slice_used` | ticks used out of the current queue's slice |
| `enter_seq` | ticket for the position inside a queue, smaller is nearer the head |
| `ctime` | tick the process was created on |
| `etime` | tick it exited on, 0 while alive |
| `rtime` | ticks spent RUNNING |
| `wtime` | ticks spent RUNNABLE (sitting in the ready queue) |
| `first_run` | tick it first got the CPU, -1 until then |

**allocproc() and kfork().** `allocproc()` sets `queue = 0`, `slice_used = 0`, `ctime = ticks`, `first_run = -1` and zeroes the rest. When `kfork()` (or `userinit()` for init) marks the process RUNNABLE it calls `requeue()`, which gives it a fresh ticket, so a new process lands at the tail of queue 0.

**Queue selection without linked lists.** There are no real queue structures. A process's place is just the pair `(queue, enter_seq)`, and tickets come from one global counter (`alloc_enter_seq()`, under `seq_lock`). The `#ifdef MLFQ` branch of `scheduler()` scans `proc[]` and picks the RUNNABLE process with the lowest queue, breaking ties by the lowest ticket. "Push to the tail of queue q" becomes "set `queue = q` and take a new ticket". This gives FIFO order inside every queue, round robin in queue 3 comes for free, and no extra lock is needed besides the `p->lock` the scan already takes. Since the scan releases each lock as it goes, the scheduler re-locks the chosen process and checks that its state, queue and ticket have not changed before running it; if another CPU got there first, it just scans again. A process that exits becomes a ZOMBIE and is never RUNNABLE again, so it drops out of selection on its own.

**Preemption (`kernel/trap.c`).** Under MLFQ, `usertrap()` and `kerneltrap()` only call `yield()` on a timer interrupt when `mlfq_tick()` returns true. `mlfq_tick()` returns true in two cases:

1. The slice is used up (`slice_used >= mlfq_slice(queue)`). The process moves down one queue (it stays in queue 3 if it is already there), `slice_used` resets and it takes a new ticket, which puts it at the tail.
2. `mlfq_higher_waiting(queue)` finds a RUNNABLE process in a strictly higher queue. In that case the process keeps its queue, its partly used slice and its ticket, so it goes back to the front of its own queue once the better process is done. Being displaced is not its fault, so only a process whose slice is really spent gets demoted.

Preemption therefore happens only at tick boundaries.

**Time slices.** `mlfq_slices[] = {1, 4, 8, 16}` in `proc.c`, read through `mlfq_slice()`. Ticks are charged by `update_time()`, which `clockintr()` calls on cpu0 once per tick, *before* `ticks++` and `wakeup(&ticks)`. It adds one to `rtime` and `slice_used` of every RUNNING process and one to `wtime` of every RUNNABLE one. Charging before the wakeup matters: a process that slept through the tick is counted as asleep, not as waiting.

Because only cpu0 charges ticks, on several CPUs a process can use up its slice between two of its own timer checks. `sleep()` repeats the slice check under `#ifdef MLFQ` and demotes the process before it sleeps, otherwise going to sleep at exactly that moment would dodge the demotion.

**Voluntary yield.** When a process sleeps (for I/O, `pause()`, `wait()` and so on) its `queue` is left alone. When `wakeup()` or `kkill()` makes it RUNNABLE again, `requeue()` resets `slice_used` and gives it a new ticket, so it rejoins the tail of the same queue with a fresh slice. Resetting the partly used slice is the simpler choice. The catch is that a process which always sleeps just before its slice ends keeps its priority, which the boost bounds.

**Priority boost.** `clockintr()` calls `mlfq_boost()` when `ticks % BOOST_INTERVAL == 0`. It moves every process that is not UNUSED or ZOMBIE to queue 0 and resets `slice_used`. Tickets are kept, so processes land in queue 0 in the same relative order they already had.

**procdump (Ctrl-P).** Under MLFQ every line gets the queue, the slice used out of that queue's slice, the ticket, the running and waiting totals and the ticks left until the next boost:

```
4 run    schedulertest  q3 slice 12/16 seq 7  run 25 wait 0  boost in 21
```

**Trace output.** With `TRACE=1`, `update_time()` prints one line per tick for each RUNNING or RUNNABLE process:

```
MLFQTRACE <tick> <pid> <queue> <1 if running, else 0>
```

Sleeping processes are not printed, so they show up as gaps. This is off by default because it does console output from inside the timer interrupt.

### FIFO and RR

**FIFO** (`SCHEDULER=FIFO`) is non-preemptive and serves a ready queue. `requeue()` hands out a ticket whenever a process becomes runnable (after fork or on wakeup), and `scheduler()` runs the RUNNABLE process with the smallest ticket. The timer interrupt never calls `yield()` under FIFO, so a process keeps the CPU until it sleeps or exits.

**RR** is the stock xv6 scheduler loop and the stock yield on every timer tick. The only changes on this path are the timing fields: `first_run` gets set in `scheduler()` and `update_time()` does the per-tick counting. None of this affects which process runs, so the policy is exactly the original one, just instrumented. That way the comparison gets the same numbers out of RR.

### The waitx syscall

```c
int waitx(int *turnaround, int *waiting, int *response, int *running);
```

It is syscall 23 (`SYS_waitx`), wired through `syscall.h`, `syscall.c`, `sysproc.c` (`sys_waitx`), `user/usys.pl` and `user/user.h`. It works like `wait()`: it blocks until a child exits, reaps it and returns its pid, or -1 if there are no children. It does not return the exit status. `kwait()` and `kwaitx()` both wrap one function, `kwait_stats()`, which copies the stats out before `freeproc()` clears the slot. Any pointer can be 0 to skip that value.

All values are in ticks:

| Metric | Definition |
|---|---|
| turnaround | `etime - ctime`, from creation to exit |
| waiting | `wtime`, ticks spent RUNNABLE |
| response | `first_run - ctime`, from creation until it first got the CPU (-1 if it never ran) |
| running | `rtime`, ticks spent RUNNING |

### schedulertest

```
schedulertest [nproc] [rounds]
```

- `nproc` defaults to 4 and is capped at 16. `rounds` defaults to 5 and is capped at 100. Values of 0 or less fall back to the defaults.
- Child `i` gets a burst of `bursts[i % 4]`, cycling through **1, 40, 150 and 600** units of work. One unit is 2,000,000 iterations of a busy loop. Under QEMU a tick is roughly 15 to 25 units, so the bursts are well under a tick, about 2 ticks, about 7 ticks and about 30 ticks, aimed at queues 0, 1, 2 and 3. The exact tick counts depend on the host, so check the trace rather than assume.
- Each child does `rounds * 625 / (burst + 25)` rounds, where one round is `burn(burst)` followed by `pause(1)`. So short bursters get more rounds (for `rounds = 5`: 120, 48, 17 and 5) and every child lives about as long as the longest one. Without this, the short ones would finish early and later boosts would only ever catch the long burster.
- The parent then calls `waitx()` once per child and prints a line per child, followed by the averages to two decimals:

```
schedulertest: 4 processes, 5 rounds
  pid 5 burst 1: turnaround ...  waiting ...  response ...  running ...
  ...
schedulertest: over 4 processes: avg turnaround ..., avg waiting ..., avg response ..., avg running ...
```

For a fair comparison, use the same arguments and the same CPU count for every scheduler.

### Reproducing the results

**Comparison.** Run the same workload under each policy on one CPU:

```bash
make clean && make qemu SCHEDULER=FIFO CPUS=1   # then: schedulertest 4 5
make clean && make qemu SCHEDULER=RR   CPUS=1   # then: schedulertest 4 5
make clean && make qemu SCHEDULER=MLFQ CPUS=1   # then: schedulertest 4 5
```

and repeat with `schedulertest 8 3` (two children per burst length).

**Trace.** Build first, so the compiler output stays out of the capture, then boot with `tee`:

```bash
make clean && make SCHEDULER=MLFQ TRACE=1 CPUS=1 kernel/kernel fs.img
make qemu SCHEDULER=MLFQ TRACE=1 CPUS=1 | tee trace.txt
# at the xv6 prompt: schedulertest 4 5, wait for the averages, then Ctrl-A X
```

The trace used in the report is committed as `xv6/trace.txt`.

**Plots.** These need Python 3 with matplotlib:

```bash
python3 plot_mlfq.py trace.txt        # optional: -o <output dir>
```

This writes four PNGs, all watermarked `chandrani.saha`:

| File | Contents |
|---|---|
| `mlfq_timeline.png` | queue of each process over time, one colour per pid, boosts marked every 48 ticks |
| `scheduler_comparison.png` | average turnaround, waiting and response for `schedulertest 4 5` |
| `scheduler_comparison_8_3.png` | the same for `schedulertest 8 3` |
| `waiting_by_burst.png` | waiting time split by burst length, for both workloads |

Only the timeline is built from the trace file. The comparison numbers are the measured averages, stored in the script. The PNGs are not kept in the repo because they are already in `report.pdf`. Running the script regenerates them.

**Results** (single CPU, averages in ticks):

| Workload | Scheduler | Turnaround | Waiting | Response |
|---|---|---|---|---|
| `schedulertest 4 5` | FIFO | 265.50 | 148.00 | 2.00 |
| | RR | 274.00 | 152.00 | 0.75 |
| | MLFQ | **197.00** | **75.25** | 0.75 |
| `schedulertest 8 3` | FIFO | 308.63 | 236.25 | 17.75 |
| | RR | 327.00 | 253.88 | 2.25 |
| | MLFQ | **239.13** | **166.88** | 2.25 |

MLFQ has the lowest turnaround and waiting on both workloads, since the short bursters stay in the high queues and hardly wait. RR matches its response time. FIFO's response time falls apart once there are more children, because a newly runnable process has to wait behind whole bursts. Timings under QEMU shift a bit between runs (the committed trace is from a separate MLFQ run and its averages differ slightly), but the ordering stays the same. The full discussion, the timeline and the per-burst breakdown are in `xv6/report.pdf`.

### Testing and known limitations

- `usertests -q` passes under MLFQ on both 1 and 3 CPUs, and under RR.
- Under FIFO, `usertests` hangs at the `preempt` test. That is by design: the test needs a spinning child to be taken off the CPU, and a non-preemptive scheduler never does that.
- Under MLFQ, running `usertests reparent` on its own right after boot can fail. The test orphans a lot of processes that init has to reap. Init gets demoted whenever it happens to be running at a tick boundary, and the stream of fresh queue 0 forks can then keep it off the CPU until the next boost while zombies fill the process table. This is strict priority starving a low queue process, which is exactly what the 48-tick boost exists to bound, so I did not special-case init. The same test passes inside the full `usertests -q` run.
- All accounting is sampled once per tick on cpu0. A process that runs for part of a tick is charged the whole tick if it is RUNNING at the sample, and nothing if it is not. That is why response times come out as small whole numbers, and a burst "well under a tick" can still show up as a tick of running time. The trace has the same granularity, and sleeping processes only appear as gaps in it.
