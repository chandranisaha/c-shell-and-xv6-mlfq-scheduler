#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define DEFAULT_PROCS  4
#define DEFAULT_ROUNDS 5
#define MAX_PROCS      16
#define INNER          2000000

// cpu burst per child, in burn() units, before it sleeps. a tick is roughly
// 15-25 units under qemu, so these are well under a tick, about 2 ticks,
// about 7 ticks and about 30 ticks, aimed at q0, q1, q2 and q3. the exact
// tick counts depend on the host, so check the trace rather than assume
static const int bursts[] = {1, 40, 150, 600};

#define NBURSTS (sizeof(bursts) / sizeof(bursts[0]))

// a round is a burst plus about a tick of sleep. short bursters get more
// rounds so every child lives about as long as the longest one, otherwise
// they finish early and later boosts only catch the long burster
#define TICK_UNITS 25
#define LONGEST    600

static void
burn(int units)
{
  volatile unsigned sink = 0;

  for (int u = 0; u < units; u++)
    for (int i = 0; i < INNER; i++)
      sink += i;
}

static void
print_avg(char *label, int total, int n)
{
  int scaled = (total * 100 + n / 2) / n;

  printf("%s %d.%d%d", label, scaled / 100, (scaled / 10) % 10, scaled % 10);
}

int
main(int argc, char *argv[])
{
  int nproc = argc > 1 ? atoi(argv[1]) : DEFAULT_PROCS;
  int rounds = argc > 2 ? atoi(argv[2]) : DEFAULT_ROUNDS;
  int pids[MAX_PROCS];
  int burst_of[MAX_PROCS];
  int started = 0;

  if (nproc <= 0)
    nproc = DEFAULT_PROCS;
  if (nproc > MAX_PROCS)
    nproc = MAX_PROCS;
  if (rounds <= 0)
    rounds = DEFAULT_ROUNDS;
  if (rounds > 100)
    rounds = 100;

  printf("schedulertest: %d processes, %d rounds\n", nproc, rounds);

  for (int i = 0; i < nproc; i++) {
    int burst = bursts[i % NBURSTS];
    int my_rounds = rounds * (LONGEST + TICK_UNITS) / (burst + TICK_UNITS);
    int pid = fork();

    if (pid < 0) {
      printf("schedulertest: fork failed after %d\n", started);
      break;
    }

    if (pid == 0) {
      // burst, then give the cpu up voluntarily, over and over
      for (int r = 0; r < my_rounds; r++) {
        burn(burst);
        pause(1);
      }
      exit(0);
    }

    pids[started] = pid;
    burst_of[started] = burst;
    started++;
  }

  int n = 0, sum_turn = 0, sum_wait = 0, sum_resp = 0, sum_run = 0;

  for (int i = 0; i < started; i++) {
    int turnaround = 0, waiting = 0, response = 0, running = 0;
    int pid = waitx(&turnaround, &waiting, &response, &running);

    if (pid < 0)
      break;

    int burst = 0;
    for (int j = 0; j < started; j++)
      if (pids[j] == pid)
        burst = burst_of[j];

    printf("  pid %d burst %d: turnaround %d  waiting %d  response %d  "
           "running %d\n",
           pid, burst, turnaround, waiting, response, running);

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
