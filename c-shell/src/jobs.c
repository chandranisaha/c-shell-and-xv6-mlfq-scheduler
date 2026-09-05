#include "jobs.h"

#include "shell.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

Job *job_create(int job_number, pid_t pgid, const char *command_line)
{
    if (command_line == NULL) {
        return NULL;
    }
    Job *job = calloc(1, sizeof(*job));
    if (job == NULL) {
        return NULL;
    }
    job->command_line = strdup(command_line);
    if (job->command_line == NULL) {
        free(job);
        return NULL;
    }
    job->job_number = job_number;
    job->pgid = pgid;
    job->state = JOB_RUNNING;
    return job;
}

void job_destroy(Job *job)
{
    if (job == NULL) {
        return;
    }
    JobProcess *process = job->processes;
    while (process != NULL) {
        JobProcess *next = process->next;
        free(process->command_name);
        free(process);
        process = next;
    }
    free(job->command_line);
    free(job);
}

void job_add(ShellState *state, Job *job)
{
    if (state == NULL || job == NULL) {
        return;
    }
    job->next = NULL;
    if (state->jobs == NULL) {
        state->jobs = job;
        return;
    }
    Job *tail = state->jobs;
    while (tail->next != NULL) {
        tail = tail->next;
    }
    tail->next = job;
}

Job *job_find_by_number(const ShellState *state, int job_number)
{
    if (state == NULL) {
        return NULL;
    }
    for (Job *job = state->jobs; job != NULL; job = job->next) {
        if (job->job_number == job_number) {
            return job;
        }
    }
    return NULL;
}

Job *job_find_by_pid(const ShellState *state, pid_t pid)
{
    if (state == NULL) {
        return NULL;
    }
    for (Job *job = state->jobs; job != NULL; job = job->next) {
        for (JobProcess *process = job->processes; process != NULL;
             process = process->next) {
            if (process->pid == pid) {
                return job;
            }
        }
    }
    return NULL;
}

Job *job_find_by_pgid(const ShellState *state, pid_t pgid)
{
    if (state == NULL) {
        return NULL;
    }
    for (Job *job = state->jobs; job != NULL; job = job->next) {
        if (job->pgid == pgid) {
            return job;
        }
    }
    return NULL;
}

int job_add_process(Job *job, pid_t pid, const char *command_name)
{
    if (job == NULL || command_name == NULL) {
        return -1;
    }
    JobProcess *process = calloc(1, sizeof(*process));
    if (process == NULL) {
        return -1;
    }
    process->command_name = strdup(command_name);
    if (process->command_name == NULL) {
        free(process);
        return -1;
    }
    process->pid = pid;
    if (job->processes == NULL) {
        job->processes = process;
    } else {
        JobProcess *tail = job->processes;
        while (tail->next != NULL) {
            tail = tail->next;
        }
        tail->next = process;
    }
    job->process_count++;
    return 0;
}

bool job_is_finished(const Job *job)
{
    if (job == NULL || job->process_count == 0) {
        return false;
    }
    for (const JobProcess *process = job->processes; process != NULL;
         process = process->next) {
        if (!process->exited) {
            return false;
        }
    }
    return true;
}

void job_update_process(Job *job, pid_t pid, int status)
{
    if (job == NULL) {
        return;
    }
    for (JobProcess *process = job->processes; process != NULL;
         process = process->next) {
        if (process->pid == pid) {
            process->status = status;
            process->exited = WIFEXITED(status) || WIFSIGNALED(status);
            process->stopped = WIFSTOPPED(status);
            if (process->stopped) {
                job->state = JOB_STOPPED;
            }
            return;
        }
    }
}

static void remove_job(ShellState *state, Job *target)
{
    Job **link = &state->jobs;
    while (*link != NULL && *link != target) {
        link = &(*link)->next;
    }
    if (*link == target) {
        *link = target->next;
        job_destroy(target);
    }
}

void jobs_reap_background(ShellState *state)
{
    if (state == NULL) {
        return;
    }
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        Job *job = job_find_by_pid(state, pid);
        if (job == NULL) {
            continue;
        }
        job_update_process(job, pid, status);
        if (!job_is_finished(job)) {
            continue;
        }

        JobProcess *first = job->processes;
        if (first != NULL && WIFEXITED(first->status)) {
            printf("%s with pid %ld exited normally\n", job->command_line,
                   (long)first->pid);
        } else {
            printf("%s with pid %ld exited abnormally\n", job->command_line,
                   (long)(first == NULL ? job->pgid : first->pid));
        }
        remove_job(state, job);
    }
}
