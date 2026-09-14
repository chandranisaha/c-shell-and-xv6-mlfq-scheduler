
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define DEFAULT_PROCS 6
#define DEFAULT_WORK  40
#define INNER         2000000

static void
burn(int units)
{
  volatile int sink = 0;

  for (int u = 0; u < units; u++)
    for (int i = 0; i < INNER; i++)
      sink += i;
}

static void
print_avg(char *label, int total, int n)
{
  int scaled = (total * 100) / n;

  printf("%s %d.%d%d", label, scaled / 100, (scaled / 10) % 10, scaled % 10);
}

int
main(int argc, char *argv[])
{
  int nproc = argc > 1 ? atoi(argv[1]) : DEFAULT_PROCS;
  int work = argc > 2 ? atoi(argv[2]) : DEFAULT_WORK;
  int started = 0;

  if (nproc <= 0)
    nproc = DEFAULT_PROCS;
  if (work <= 0)
    work = DEFAULT_WORK;

  printf("schedulertest: %d processes, work=%d\n", nproc, work);

  for (int i = 0; i < nproc; i++) {
    int pid = fork();

    if (pid < 0) {
      printf("schedulertest: fork failed after %d\n", started);
      break;
    }

    if (pid == 0) {
      if (i % 2 == 0) {

        burn(work);
      } else {

        int cycles = work / 5;

        if (cycles < 1)
          cycles = 1;

        for (int k = 0; k < cycles; k++) {
          burn(1);
          pause(1);
        }
      }
      exit(0);
    }

    started++;
  }

  int n = 0, sum_turn = 0, sum_wait = 0, sum_resp = 0, sum_run = 0;

  for (int i = 0; i < started; i++) {
    int turnaround = 0, waiting = 0, response = 0, running = 0;

    if (waitx(&turnaround, &waiting, &response, &running) < 0)
      break;

    printf("  child %d: turnaround %d  waiting %d  response %d  running %d\n",
           n, turnaround, waiting, response, running);

    sum_turn += turnaround;
    sum_wait += waiting;
    sum_resp += response;
    sum_run += running;
    n++;
  }

  if (n > 0) {
    printf("schedulertest: over %d processes -- ", n);
    print_avg("avg turnaround", sum_turn, n);
    print_avg(", avg waiting", sum_wait, n);
    print_avg(", avg response", sum_resp, n);
    print_avg(", avg running", sum_run, n);
    printf("\n");
  }

  exit(0);
}
