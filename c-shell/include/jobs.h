#ifndef CSHELL_JOBS_H
#define CSHELL_JOBS_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

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

#endif
