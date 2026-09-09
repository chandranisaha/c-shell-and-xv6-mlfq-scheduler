#ifndef CSHELL_SIGNALS_H
#define CSHELL_SIGNALS_H

void signals_install(void);
int signals_pending(void);
void signals_clear(void);

/* Makes the shell itself immune to terminal-generated job-control signals,
 * per E2: SIGINT/SIGTSTP/SIGTTOU must never kill or suspend the shell
 * process, only whichever process group currently owns the terminal (a
 * foreground job). SIGTSTP and SIGTTOU are ignored outright; SIGINT gets a
 * flag-only handler instead, so that Ctrl-C at an idle prompt can still be
 * noticed and answered with a fresh prompt. */
void signals_ignore_terminal(void);

/* Set when Ctrl-C reached the shell itself (i.e. no foreground job owned
 * the terminal). Checked by input_read_line() on EINTR, which turns it
 * into INPUT_INTERRUPTED so the main loop can redraw the prompt. */
int signals_interrupt_pending(void);
void signals_clear_interrupt(void);

/* A forked child must call this before execve()/running a builtin: fork()
 * inherits the shell's SIG_IGN disposition for these three signals, and
 * exec() does NOT reset an ignored signal back to default, so without this
 * every foreground job would silently ignore Ctrl-C/Ctrl-Z too. */
void signals_restore_terminal_defaults(void);

/* E3's `resume ... fg --timeout`: a minimal SIGALRM handler that only sets
 * a flag, the same pattern as SIGCHLD above. Install once; the flag is
 * checked after a waitpid() that alarm() was set up to interrupt. */
void signals_install_alarm(void);
int signals_alarm_fired(void);
void signals_clear_alarm(void);

#endif
