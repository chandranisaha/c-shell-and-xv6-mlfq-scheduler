#include "activities.h"

#include "jobs.h"

#include <stdio.h>

static const char *display_name(const char *name)
{
    return (name != NULL && name[0] == '%') ? name + 1 : name;
}

int activities_execute(ShellState *state, const TokenList *tokens)
{
    (void)tokens;
    if (state == NULL) {
        return -1;
    }

    /* Processes that have exited must be removed before printing. */
    jobs_reap_background(state);

    for (const Job *job = state->jobs; job != NULL; job = job->next) {
        printf("[%d] pgid %ld\n", job->job_number, (long)job->pgid);
        for (const JobProcess *process = job->processes; process != NULL;
             process = process->next) {
            if (process->exited) {
                continue;
            }
            const char *state_word = process->stopped ? "Stopped" : "Running";
            printf("  %ld %s %s\n", (long)process->pid,
                   display_name(process->command_name), state_word);
        }
    }
    fflush(stdout);
    return 0;
}
