#include "signals.h"

#include <signal.h>
#include <string.h>

static volatile sig_atomic_t sigchld_pending;
static volatile sig_atomic_t alarm_fired;

static void handle_sigchld(int signal_number)
{
    (void)signal_number;
    sigchld_pending = 1;
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
    action.sa_handler = SIG_IGN;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    (void)sigaction(SIGINT, &action, NULL);
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
