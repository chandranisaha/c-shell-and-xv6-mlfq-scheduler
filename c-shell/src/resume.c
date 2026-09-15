#include "resume.h"

#include "exec.h"
#include "jobs.h"
#include "signals.h"

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

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

static void mark_job_running(Job *job)
{
    job->state = JOB_RUNNING;
    for (JobProcess *process = job->processes; process != NULL;
         process = process->next) {
        process->stopped = false;
    }
}

static bool wait_for_job(Job *job, long timeout_seconds, bool *timed_out,
                         bool *any_signaled)
{
    bool use_timeout = timeout_seconds >= 0;
    bool any_stopped = false;
    *timed_out = false;
    *any_signaled = false;

    if (use_timeout && timeout_seconds == 0) {

        (void)kill(-job->pgid, SIGTERM);
        *timed_out = true;
        use_timeout = false;
    } else if (use_timeout) {
        signals_clear_alarm();
        alarm((unsigned int)timeout_seconds);
    }

    for (JobProcess *process = job->processes; process != NULL;
         process = process->next) {
        if (process->exited) {
            continue;
        }
        int status;
        pid_t waited;
        for (;;) {
            waited = waitpid(process->pid, &status, WUNTRACED);
            if (waited >= 0) {
                break;
            }
            if (errno != EINTR) {
                break;
            }
            if (use_timeout && signals_alarm_fired() && !*timed_out) {
                *timed_out = true;
                (void)kill(-job->pgid, SIGTERM);
            }
        }
        if (waited < 0) {
            continue;
        }
        job_update_process(job, process->pid, status);
        if (WIFSTOPPED(status)) {
            any_stopped = true;
        }
        if (WIFSIGNALED(status)) {
            *any_signaled = true;
        }
    }

    if (use_timeout) {
        alarm(0);
        signals_clear_alarm();
    }
    return any_stopped;
}

int resume_execute(ShellState *state, const TokenList *tokens)
{
    if (state == NULL || tokens == NULL || tokens->count == 0 ||
        tokens->items[0].type != TOKEN_WORD ||
        strcmp(tokens->items[0].value, "resume") != 0) {
        return -1;
    }

    for (size_t index = 1; index < tokens->count; index++) {
        if (tokens->items[index].type != TOKEN_WORD) {
            printf("resume: invalid syntax\n");
            return 0;
        }
    }

    if (tokens->count < 3) {
        printf("resume: invalid syntax\n");
        return 0;
    }

    const char *job_arg = tokens->items[1].value;
    long job_number = 0;
    if (job_arg[0] != '%' || !parse_non_negative_long(job_arg + 1, &job_number)) {
        printf("resume: invalid syntax\n");
        return 0;
    }

    const char *mode = tokens->items[2].value;
    bool is_bg = strcmp(mode, "bg") == 0;
    bool is_fg = strcmp(mode, "fg") == 0;
    if (!is_bg && !is_fg) {
        printf("resume: invalid syntax\n");
        return 0;
    }

    long timeout_seconds = -1;
    if (is_bg) {
        if (tokens->count != 3) {
            printf("resume: invalid syntax\n");
            return 0;
        }
    } else if (tokens->count == 5) {
        if (strcmp(tokens->items[3].value, "--timeout") != 0 ||
            !parse_non_negative_long(tokens->items[4].value, &timeout_seconds)) {
            printf("resume: invalid syntax\n");
            return 0;
        }
    } else if (tokens->count != 3) {
        printf("resume: invalid syntax\n");
        return 0;
    }

    Job *job = job_find_by_number(state, (int)job_number);
    if (job == NULL) {
        printf("resume: no such job\n");
        return 0;
    }

    (void)kill(-job->pgid, SIGCONT);
    mark_job_running(job);

    if (is_bg) {
        printf("[%d] + Running    %s\n", job->job_number, job->command_line);
        fflush(stdout);
        return 0;
    }

    printf("%s\n", job->command_line);
    fflush(stdout);
    state->foreground_pgid = job->pgid;
    give_terminal(state, job->pgid);

    bool timed_out = false;
    bool any_signaled = false;
    bool any_stopped =
        wait_for_job(job, timeout_seconds, &timed_out, &any_signaled);

    give_terminal(state, state->shell_pgid);
    state->foreground_pgid = 0;

    if (timed_out) {
        printf("resume: job timed out\n");
        fflush(stdout);
        if (job_is_finished(job)) {
            job_remove(state, job);
        }
        return 0;
    }

    if (any_stopped) {

        putchar('\n');
        printf("[%d] + Stopped %s\n", job->job_number, job->command_line);
        fflush(stdout);
    } else if (any_signaled) {

        putchar('\n');
        fflush(stdout);
        if (job_is_finished(job)) {
            job_remove(state, job);
        }
    } else if (job_is_finished(job)) {
        job_remove(state, job);
    }
    return 0;
}
