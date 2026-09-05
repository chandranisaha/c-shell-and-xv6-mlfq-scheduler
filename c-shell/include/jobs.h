#ifndef CSHELL_JOBS_H
#define CSHELL_JOBS_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

typedef struct ShellState ShellState;

typedef enum {
    JOB_RUNNING,
    JOB_STOPPED
} JobState;

typedef struct JobProcess {
    pid_t pid;
    char *command_name;
    bool exited;
    bool stopped;
    int status;
    struct JobProcess *next;
} JobProcess;

typedef struct Job {
    int job_number;
    pid_t pgid;
    char *command_line;
    JobState state;
    JobProcess *processes;
    size_t process_count;
    struct Job *next;
} Job;

Job *job_create(int job_number, pid_t pgid, const char *command_line);
void job_destroy(Job *job);
void job_add(ShellState *state, Job *job);
Job *job_find_by_number(const ShellState *state, int job_number);
Job *job_find_by_pid(const ShellState *state, pid_t pid);
Job *job_find_by_pgid(const ShellState *state, pid_t pgid);
int job_add_process(Job *job, pid_t pid, const char *command_name);
bool job_is_finished(const Job *job);

#endif
