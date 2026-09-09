#include "signals.h"

#include <signal.h>
#include <string.h>

static volatile sig_atomic_t sigchld_pending;
static volatile sig_atomic_t sigint_pending;
static volatile sig_atomic_t alarm_fired;

static void handle_sigchld(int signal_number)
{
    (void)signal_number;
    sigchld_pending = 1;
}

static void handle_sigint(int signal_number)
{
    (void)signal_number;
    sigint_pending = 1;
}

static void handle_alarm(int signal_number)
{
    (void)signal_number;
    alarm_fired = 1;
}

void signals_install(void)
{
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = handle_sigchld;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    (void)sigaction(SIGCHLD, &action, NULL);
}

int signals_pending(void)
{
    return sigchld_pending != 0;
}

void signals_clear(void)
{
    sigchld_pending = 0;
}

void signals_ignore_terminal(void)
{
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

    /* Q54: SIGINT gets a real handler, not SIG_IGN. "Ignore" in the
     * writeup only means the shell must not *die* from Ctrl-C; the E2
     * example still shows a fresh prompt appearing after ^C, which is
     * impossible if the signal is discarded outright. Flag-only handler,
     * with sa_flags = 0 deliberately: no SA_RESTART, so the blocked
     * read() in input_read_line() returns EINTR and can act on it. */
    action.sa_handler = handle_sigint;
    (void)sigaction(SIGINT, &action, NULL);

    /* These two stay ignored. Ctrl-Z at the prompt should do nothing, and
     * ignoring SIGTTOU is what lets tcsetpgrp() hand the terminal around
     * without the shell suspending itself. */
    action.sa_handler = SIG_IGN;
    (void)sigaction(SIGTSTP, &action, NULL);
    (void)sigaction(SIGTTOU, &action, NULL);
}

void signals_restore_terminal_defaults(void)
{
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = SIG_DFL;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    (void)sigaction(SIGINT, &action, NULL);
    (void)sigaction(SIGTSTP, &action, NULL);
    (void)sigaction(SIGTTOU, &action, NULL);
}

int signals_interrupt_pending(void)
{
    return sigint_pending != 0;
}

void signals_clear_interrupt(void)
{
    sigint_pending = 0;
}

void signals_install_alarm(void)
{
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = handle_alarm;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    (void)sigaction(SIGALRM, &action, NULL);
}

int signals_alarm_fired(void)
{
    return alarm_fired != 0;
}

void signals_clear_alarm(void)
{
    alarm_fired = 0;
}
