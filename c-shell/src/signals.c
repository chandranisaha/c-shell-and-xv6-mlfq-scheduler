#include "signals.h"

#include <signal.h>
#include <string.h>

static volatile sig_atomic_t sigchld_pending;

static void handle_sigchld(int signal_number)
{
    (void)signal_number;
    sigchld_pending = 1;
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
