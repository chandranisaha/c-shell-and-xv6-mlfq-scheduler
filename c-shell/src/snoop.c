#include "snoop.h"

#include "exec.h"
#include "signals.h"

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAX_TRACKED 512

extern char **environ;

typedef struct {
    long number;
    long calls;
    double seconds;
    long first_seen;
    bool in_call;
    struct timespec entered;
} SyscallStat;

static SyscallStat stats[MAX_TRACKED];
static size_t stat_count;
static long next_sequence;

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

static void stats_reset(void)
{
    stat_count = 0;
    next_sequence = 0;
}

static SyscallStat *stat_for(long number)
{
    for (size_t index = 0; index < stat_count; index++) {
        if (stats[index].number == number) {
            return &stats[index];
        }
    }
    if (stat_count == MAX_TRACKED) {
        return NULL;
    }
    SyscallStat *entry = &stats[stat_count++];
    entry->number = number;
    entry->calls = 0;
    entry->seconds = 0.0;
    entry->first_seen = next_sequence++;
    entry->in_call = false;
    return entry;
}

static double elapsed(const struct timespec *from, const struct timespec *to)
{
    return (double)(to->tv_sec - from->tv_sec) +
           (double)(to->tv_nsec - from->tv_nsec) / 1e9;
}

static int stat_compare(const void *left, const void *right)
{
    const SyscallStat *a = left;
    const SyscallStat *b = right;

    if (a->calls != b->calls) {
        return a->calls > b->calls ? -1 : 1;
    }
    return a->first_seen < b->first_seen ? -1 : 1;
}

typedef struct {
    long number;
    const char *name;
} SyscallName;

// each guarded so the table still builds if a constant is missing
static const SyscallName syscall_names[] = {
#ifdef SYS_read
    {SYS_read, "read"},
#endif
#ifdef SYS_write
    {SYS_write, "write"},
#endif
#ifdef SYS_open
    {SYS_open, "open"},
#endif
#ifdef SYS_close
    {SYS_close, "close"},
#endif
#ifdef SYS_stat
    {SYS_stat, "stat"},
#endif
#ifdef SYS_fstat
    {SYS_fstat, "fstat"},
#endif
#ifdef SYS_lstat
    {SYS_lstat, "lstat"},
#endif
#ifdef SYS_poll
    {SYS_poll, "poll"},
#endif
#ifdef SYS_lseek
    {SYS_lseek, "lseek"},
#endif
#ifdef SYS_mmap
    {SYS_mmap, "mmap"},
#endif
#ifdef SYS_mprotect
    {SYS_mprotect, "mprotect"},
#endif
#ifdef SYS_munmap
    {SYS_munmap, "munmap"},
#endif
#ifdef SYS_brk
    {SYS_brk, "brk"},
#endif
#ifdef SYS_rt_sigaction
    {SYS_rt_sigaction, "rt_sigaction"},
#endif
#ifdef SYS_rt_sigprocmask
    {SYS_rt_sigprocmask, "rt_sigprocmask"},
#endif
#ifdef SYS_rt_sigreturn
    {SYS_rt_sigreturn, "rt_sigreturn"},
#endif
#ifdef SYS_ioctl
    {SYS_ioctl, "ioctl"},
#endif
#ifdef SYS_pread64
    {SYS_pread64, "pread64"},
#endif
#ifdef SYS_pwrite64
    {SYS_pwrite64, "pwrite64"},
#endif
#ifdef SYS_readv
    {SYS_readv, "readv"},
#endif
#ifdef SYS_writev
    {SYS_writev, "writev"},
#endif
#ifdef SYS_access
    {SYS_access, "access"},
#endif
#ifdef SYS_pipe
    {SYS_pipe, "pipe"},
#endif
#ifdef SYS_select
    {SYS_select, "select"},
#endif
#ifdef SYS_sched_yield
    {SYS_sched_yield, "sched_yield"},
#endif
#ifdef SYS_mremap
    {SYS_mremap, "mremap"},
#endif
#ifdef SYS_msync
    {SYS_msync, "msync"},
#endif
#ifdef SYS_madvise
    {SYS_madvise, "madvise"},
#endif
#ifdef SYS_dup
    {SYS_dup, "dup"},
#endif
#ifdef SYS_dup2
    {SYS_dup2, "dup2"},
#endif
#ifdef SYS_nanosleep
    {SYS_nanosleep, "nanosleep"},
#endif
#ifdef SYS_clock_nanosleep
    {SYS_clock_nanosleep, "clock_nanosleep"},
#endif
#ifdef SYS_getpid
    {SYS_getpid, "getpid"},
#endif
#ifdef SYS_socket
    {SYS_socket, "socket"},
#endif
#ifdef SYS_connect
    {SYS_connect, "connect"},
#endif
#ifdef SYS_sendto
    {SYS_sendto, "sendto"},
#endif
#ifdef SYS_recvfrom
    {SYS_recvfrom, "recvfrom"},
#endif
#ifdef SYS_clone
    {SYS_clone, "clone"},
#endif
#ifdef SYS_fork
    {SYS_fork, "fork"},
#endif
#ifdef SYS_vfork
    {SYS_vfork, "vfork"},
#endif
#ifdef SYS_execve
    {SYS_execve, "execve"},
#endif
#ifdef SYS_exit
    {SYS_exit, "exit"},
#endif
#ifdef SYS_wait4
    {SYS_wait4, "wait4"},
#endif
#ifdef SYS_kill
    {SYS_kill, "kill"},
#endif
#ifdef SYS_uname
    {SYS_uname, "uname"},
#endif
#ifdef SYS_fcntl
    {SYS_fcntl, "fcntl"},
#endif
#ifdef SYS_getdents64
    {SYS_getdents64, "getdents64"},
#endif
#ifdef SYS_getcwd
    {SYS_getcwd, "getcwd"},
#endif
#ifdef SYS_chdir
    {SYS_chdir, "chdir"},
#endif
#ifdef SYS_rename
    {SYS_rename, "rename"},
#endif
#ifdef SYS_mkdir
    {SYS_mkdir, "mkdir"},
#endif
#ifdef SYS_unlink
    {SYS_unlink, "unlink"},
#endif
#ifdef SYS_readlink
    {SYS_readlink, "readlink"},
#endif
#ifdef SYS_gettimeofday
    {SYS_gettimeofday, "gettimeofday"},
#endif
#ifdef SYS_getuid
    {SYS_getuid, "getuid"},
#endif
#ifdef SYS_getgid
    {SYS_getgid, "getgid"},
#endif
#ifdef SYS_geteuid
    {SYS_geteuid, "geteuid"},
#endif
#ifdef SYS_getegid
    {SYS_getegid, "getegid"},
#endif
#ifdef SYS_getppid
    {SYS_getppid, "getppid"},
#endif
#ifdef SYS_sigaltstack
    {SYS_sigaltstack, "sigaltstack"},
#endif
#ifdef SYS_statfs
    {SYS_statfs, "statfs"},
#endif
#ifdef SYS_arch_prctl
    {SYS_arch_prctl, "arch_prctl"},
#endif
#ifdef SYS_gettid
    {SYS_gettid, "gettid"},
#endif
#ifdef SYS_futex
    {SYS_futex, "futex"},
#endif
#ifdef SYS_set_tid_address
    {SYS_set_tid_address, "set_tid_address"},
#endif
#ifdef SYS_clock_gettime
    {SYS_clock_gettime, "clock_gettime"},
#endif
#ifdef SYS_exit_group
    {SYS_exit_group, "exit_group"},
#endif
#ifdef SYS_openat
    {SYS_openat, "openat"},
#endif
#ifdef SYS_newfstatat
    {SYS_newfstatat, "newfstatat"},
#endif
#ifdef SYS_set_robust_list
    {SYS_set_robust_list, "set_robust_list"},
#endif
#ifdef SYS_prlimit64
    {SYS_prlimit64, "prlimit64"},
#endif
#ifdef SYS_getrandom
    {SYS_getrandom, "getrandom"},
#endif
#ifdef SYS_rseq
    {SYS_rseq, "rseq"},
#endif
#ifdef SYS_faccessat2
    {SYS_faccessat2, "faccessat2"},
#endif
#ifdef SYS_restart_syscall
    {SYS_restart_syscall, "restart_syscall"},
#endif
#ifdef SYS_statx
    {SYS_statx, "statx"},
#endif
#ifdef SYS_sched_getaffinity
    {SYS_sched_getaffinity, "sched_getaffinity"},
#endif
#ifdef SYS_prctl
    {SYS_prctl, "prctl"},
#endif
#ifdef SYS_clone3
    {SYS_clone3, "clone3"},
#endif
#ifdef SYS_pipe2
    {SYS_pipe2, "pipe2"},
#endif
#ifdef SYS_dup3
    {SYS_dup3, "dup3"},
#endif
#ifdef SYS_faccessat
    {SYS_faccessat, "faccessat"},
#endif
#ifdef SYS_readlinkat
    {SYS_readlinkat, "readlinkat"},
#endif
#ifdef SYS_unlinkat
    {SYS_unlinkat, "unlinkat"},
#endif
#ifdef SYS_mkdirat
    {SYS_mkdirat, "mkdirat"},
#endif
#ifdef SYS_renameat2
    {SYS_renameat2, "renameat2"},
#endif
#ifdef SYS_fchdir
    {SYS_fchdir, "fchdir"},
#endif
#ifdef SYS_fsync
    {SYS_fsync, "fsync"},
#endif
#ifdef SYS_ftruncate
    {SYS_ftruncate, "ftruncate"},
#endif
#ifdef SYS_getdents
    {SYS_getdents, "getdents"},
#endif
#ifdef SYS_fchmod
    {SYS_fchmod, "fchmod"},
#endif
#ifdef SYS_fchown
    {SYS_fchown, "fchown"},
#endif
#ifdef SYS_umask
    {SYS_umask, "umask"},
#endif
#ifdef SYS_getrlimit
    {SYS_getrlimit, "getrlimit"},
#endif
#ifdef SYS_getrusage
    {SYS_getrusage, "getrusage"},
#endif
#ifdef SYS_sysinfo
    {SYS_sysinfo, "sysinfo"},
#endif
#ifdef SYS_times
    {SYS_times, "times"},
#endif
#ifdef SYS_setpgid
    {SYS_setpgid, "setpgid"},
#endif
#ifdef SYS_getpgrp
    {SYS_getpgrp, "getpgrp"},
#endif
#ifdef SYS_getpgid
    {SYS_getpgid, "getpgid"},
#endif
#ifdef SYS_setsid
    {SYS_setsid, "setsid"},
#endif
#ifdef SYS_getsid
    {SYS_getsid, "getsid"},
#endif
#ifdef SYS_setuid
    {SYS_setuid, "setuid"},
#endif
#ifdef SYS_setgid
    {SYS_setgid, "setgid"},
#endif
#ifdef SYS_getgroups
    {SYS_getgroups, "getgroups"},
#endif
#ifdef SYS_rt_sigsuspend
    {SYS_rt_sigsuspend, "rt_sigsuspend"},
#endif
#ifdef SYS_pause
    {SYS_pause, "pause"},
#endif
#ifdef SYS_alarm
    {SYS_alarm, "alarm"},
#endif
#ifdef SYS_setitimer
    {SYS_setitimer, "setitimer"},
#endif
#ifdef SYS_getitimer
    {SYS_getitimer, "getitimer"},
#endif
#ifdef SYS_tgkill
    {SYS_tgkill, "tgkill"},
#endif
#ifdef SYS_waitid
    {SYS_waitid, "waitid"},
#endif
#ifdef SYS_pselect6
    {SYS_pselect6, "pselect6"},
#endif
#ifdef SYS_ppoll
    {SYS_ppoll, "ppoll"},
#endif
#ifdef SYS_epoll_create1
    {SYS_epoll_create1, "epoll_create1"},
#endif
#ifdef SYS_epoll_ctl
    {SYS_epoll_ctl, "epoll_ctl"},
#endif
#ifdef SYS_epoll_wait
    {SYS_epoll_wait, "epoll_wait"},
#endif
#ifdef SYS_epoll_pwait
    {SYS_epoll_pwait, "epoll_pwait"},
#endif
#ifdef SYS_eventfd2
    {SYS_eventfd2, "eventfd2"},
#endif
#ifdef SYS_accept
    {SYS_accept, "accept"},
#endif
#ifdef SYS_accept4
    {SYS_accept4, "accept4"},
#endif
#ifdef SYS_bind
    {SYS_bind, "bind"},
#endif
#ifdef SYS_listen
    {SYS_listen, "listen"},
#endif
#ifdef SYS_sendmsg
    {SYS_sendmsg, "sendmsg"},
#endif
#ifdef SYS_recvmsg
    {SYS_recvmsg, "recvmsg"},
#endif
#ifdef SYS_shutdown
    {SYS_shutdown, "shutdown"},
#endif
#ifdef SYS_getsockname
    {SYS_getsockname, "getsockname"},
#endif
#ifdef SYS_getpeername
    {SYS_getpeername, "getpeername"},
#endif
#ifdef SYS_socketpair
    {SYS_socketpair, "socketpair"},
#endif
#ifdef SYS_setsockopt
    {SYS_setsockopt, "setsockopt"},
#endif
#ifdef SYS_getsockopt
    {SYS_getsockopt, "getsockopt"},
#endif
#ifdef SYS_fadvise64
    {SYS_fadvise64, "fadvise64"},
#endif
#ifdef SYS_sendfile
    {SYS_sendfile, "sendfile"},
#endif
#ifdef SYS_splice
    {SYS_splice, "splice"},
#endif
#ifdef SYS_copy_file_range
    {SYS_copy_file_range, "copy_file_range"},
#endif
#ifdef SYS_fallocate
    {SYS_fallocate, "fallocate"},
#endif
#ifdef SYS_utimensat
    {SYS_utimensat, "utimensat"},
#endif
#ifdef SYS_fchmodat
    {SYS_fchmodat, "fchmodat"},
#endif
#ifdef SYS_fchownat
    {SYS_fchownat, "fchownat"},
#endif
#ifdef SYS_symlinkat
    {SYS_symlinkat, "symlinkat"},
#endif
#ifdef SYS_linkat
    {SYS_linkat, "linkat"},
#endif
#ifdef SYS_execveat
    {SYS_execveat, "execveat"},
#endif
#ifdef SYS_clock_getres
    {SYS_clock_getres, "clock_getres"},
#endif
#ifdef SYS_timerfd_create
    {SYS_timerfd_create, "timerfd_create"},
#endif
#ifdef SYS_inotify_init1
    {SYS_inotify_init1, "inotify_init1"},
#endif
#ifdef SYS_membarrier
    {SYS_membarrier, "membarrier"},
#endif
#ifdef SYS_mlock
    {SYS_mlock, "mlock"},
#endif
#ifdef SYS_sched_setaffinity
    {SYS_sched_setaffinity, "sched_setaffinity"},
#endif
#ifdef SYS_capget
    {SYS_capget, "capget"},
#endif
#ifdef SYS_getxattr
    {SYS_getxattr, "getxattr"},
#endif
#ifdef SYS_lgetxattr
    {SYS_lgetxattr, "lgetxattr"},
#endif
#ifdef SYS_fgetxattr
    {SYS_fgetxattr, "fgetxattr"},
#endif
#ifdef SYS_listxattr
    {SYS_listxattr, "listxattr"},
#endif
#ifdef SYS_llistxattr
    {SYS_llistxattr, "llistxattr"},
#endif
};

static const char *name_for(long number, char *fallback, size_t size)
{
    for (size_t index = 0;
         index < sizeof(syscall_names) / sizeof(syscall_names[0]); index++) {
        if (syscall_names[index].number == number) {
            return syscall_names[index].name;
        }
    }
    snprintf(fallback, size, "syscall_%ld", number);
    return fallback;
}

static void print_summary(void)
{
    qsort(stats, stat_count, sizeof(stats[0]), stat_compare);

    int width = 15;
    for (size_t index = 0; index < stat_count; index++) {
        char fallback[32];
        int length = (int)strlen(
            name_for(stats[index].number, fallback, sizeof(fallback)));
        if (length > width) {
            width = length;
        }
    }

    printf("%-*s %-7s %s\n", width, "syscall", "calls", "time");
    for (size_t index = 0; index < stat_count; index++) {
        char fallback[32];
        printf("%-*s %-7ld %.3fs\n", width,
               name_for(stats[index].number, fallback, sizeof(fallback)),
               stats[index].calls, stats[index].seconds);
    }
    fflush(stdout);
}

// one stop per syscall entry and one per exit, marked with 0x80 by
// TRACESYSGOOD. any other stop is a signal meant for the tracee, so it is
// passed on, except job control stops which would leave snoop waiting forever
static void trace_loop(pid_t tracee, bool detach_on_interrupt)
{
    int pending_signal = 0;

    (void)ptrace(PTRACE_SETOPTIONS, tracee, 0, (void *)PTRACE_O_TRACESYSGOOD);

    for (;;) {
        if (ptrace(PTRACE_SYSCALL, tracee, 0, (void *)(long)pending_signal) !=
            0) {
            break;
        }
        pending_signal = 0;

        int status;
        pid_t waited;
        // an idle tracee makes no syscalls, so this blocks until ctrl-c.
        // the interrupt must be checked inside the EINTR retry or it is
        // swallowed and the shell hangs.
        for (;;) {
            waited = waitpid(tracee, &status, 0);
            if (waited >= 0 || errno != EINTR) {
                break;
            }
            if (detach_on_interrupt && signals_interrupt_pending()) {
                break;
            }
        }

        if (detach_on_interrupt && signals_interrupt_pending()) {
            signals_clear_interrupt();
            (void)ptrace(PTRACE_DETACH, tracee, 0, 0);
            putchar('\n');
            break;
        }

        if (waited < 0 || WIFEXITED(status) || WIFSIGNALED(status)) {
            break;
        }

        if (!WIFSTOPPED(status)) {
            continue;
        }

        int stop = WSTOPSIG(status);
        if (stop != (SIGTRAP | 0x80)) {
            if (stop != SIGTRAP && stop != SIGSTOP && stop != SIGTSTP &&
                stop != SIGTTIN && stop != SIGTTOU) {
                pending_signal = stop;
            }
            continue;
        }

        struct user_regs_struct regs;
        if (ptrace(PTRACE_GETREGS, tracee, 0, &regs) != 0) {
            break;
        }

        SyscallStat *entry = stat_for((long)regs.orig_rax);
        if (entry == NULL) {
            continue;
        }

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);

        if (!entry->in_call) {
            entry->in_call = true;
            entry->entered = now;
            entry->calls++;
        } else {
            entry->in_call = false;
            entry->seconds += elapsed(&entry->entered, &now);
        }
    }
}

static int snoop_launch(const char *path, const TokenList *tokens)
{
    char *argv[MAX_ARGS + 1];
    size_t argc = 0;

    for (size_t index = 1; index < tokens->count && argc < MAX_ARGS; index++) {
        argv[argc++] = tokens->items[index].value;
    }
    argv[argc] = NULL;

    pid_t child = fork();
    if (child < 0) {
        fprintf(stderr, "snoop: unable to fork\n");
        return 0;
    }

    if (child == 0) {
        signals_restore_terminal_defaults();
        if (ptrace(PTRACE_TRACEME, 0, 0, 0) != 0) {
            _exit(127);
        }
        execve(path, argv, environ);
        _exit(127);
    }

    int status;
    pid_t waited;
    do {
        waited = waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR);

    if (waited < 0) {
        return 0;
    }

    trace_loop(child, false);
    print_summary();
    return 0;
}

static int snoop_attach(pid_t pid)
{
    if (ptrace(PTRACE_ATTACH, pid, 0, 0) != 0) {
        // yama ptrace_scope=1 denies attaching to anything but a descendant
        if (errno == ESRCH) {
            fprintf(stderr, "snoop: no such process\n");
        } else {
            fprintf(stderr, "snoop: cannot attach to process\n");
        }
        return 0;
    }

    int status;
    pid_t waited;
    do {
        waited = waitpid(pid, &status, 0);
    } while (waited < 0 && errno == EINTR);

    if (waited < 0) {
        fprintf(stderr, "snoop: no such process\n");
        return 0;
    }

    signals_clear_interrupt();
    trace_loop(pid, true);
    print_summary();
    return 0;
}

int snoop_execute(const ShellState *state, const TokenList *tokens)
{
    if (state == NULL || tokens == NULL || tokens->count == 0 ||
        tokens->items[0].type != TOKEN_WORD ||
        strcmp(tokens->items[0].value, "snoop") != 0) {
        return -1;
    }

    for (size_t index = 1; index < tokens->count; index++) {
        if (tokens->items[index].type != TOKEN_WORD) {
            fprintf(stderr, "snoop: invalid syntax\n");
            return 0;
        }
    }

    if (tokens->count < 2) {
        fprintf(stderr, "snoop: invalid syntax\n");
        return 0;
    }

    stats_reset();

    if (strcmp(tokens->items[1].value, "-p") == 0) {
        long pid = 0;
        if (tokens->count != 3 ||
            !parse_non_negative_long(tokens->items[2].value, &pid)) {
            fprintf(stderr, "snoop: invalid syntax\n");
            return 0;
        }
        if (kill((pid_t)pid, 0) != 0 && errno == ESRCH) {
            fprintf(stderr, "snoop: no such process\n");
            return 0;
        }
        return snoop_attach((pid_t)pid);
    }

    char *resolved = resolve_cmd_path(tokens->items[1].value);
    if (resolved == NULL) {
        fprintf(stderr, "snoop: command not found\n");
        return 0;
    }

    int result = snoop_launch(resolved, tokens);
    free(resolved);
    return result;
}
