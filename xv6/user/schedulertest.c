// Workload generator for exercising the scheduler. Spawns a mix of cpu-bound
// children (which should sink through the queues) and io-bound children
// (which sleep before their slice runs out and so should stay put), then
// waits for the lot. Hit ctrl-p while it runs to watch the queues move.
//
//   schedulertest [nproc] [work]
//
// nproc defaults to 5, work scales how long each burst is.

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define DEFAULT_PROCS 5
#define DEFAULT_WORK  8
#define INNER         2000000

// Spin for roughly `units` bursts of cpu work. volatile so the compiler
// cannot notice the result is unused and delete the whole loop.
static void
burn(int units)
{
  volatile int sink = 0;

  for (int u = 0; u < units; u++)
    for (int i = 0; i < INNER; i++)
      sink += i;
}

int
main(int argc, char *argv[])
{
  int nproc = argc > 1 ? atoi(argv[1]) : DEFAULT_PROCS;
  int work = argc > 2 ? atoi(argv[2]) : DEFAULT_WORK;

  if (nproc <= 0)
    nproc = DEFAULT_PROCS;
  if (work <= 0)
    work = DEFAULT_WORK;

  printf("schedulertest: %d processes, work=%d\n", nproc, work);

  for (int i = 0; i < nproc; i++) {
    int pid = fork();

    if (pid < 0) {
      printf("schedulertest: fork failed\n");
      break;
    }

    if (pid == 0) {
      if (i % 2 == 0) {
        // cpu bound: never gives the cpu up voluntarily, so it should walk
        // 0 -> 1 -> 2 -> 3 and stay at the bottom until a boost.
        burn(work);
      } else {
        // io bound: a short burst then a sleep, over and over. each burst is
        // meant to be shorter than the queue-0 slice, so it should keep its
        // priority instead of sinking. this xv6 revision spells sleep(2)
        // as pause().
        for (int k = 0; k < work; k++) {
          burn(1);
          pause(1);
        }
      }
      exit(0);
    }
  }

  for (int i = 0; i < nproc; i++)
    wait(0);

  printf("schedulertest: done\n");
  exit(0);
}
