#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

// Rather than four real linked lists, a process carries its queue number and
// a ticket taken from this counter. "Push to the tail of queue q" is then just
// "set queue = q and take a fresh ticket", and picking the next process to run
// is "lowest queue, and among those the lowest ticket". That is exact FIFO
// order inside every queue without a second lock to get wrong.
uint64 next_enter_seq = 1;
struct spinlock seq_lock;

// ticks a process may run for before it drops a queue, indexed by queue.
static const int mlfq_slices[NQUEUE] = {1, 4, 8, 16};

int
mlfq_slice(int queue)
{
  if (queue < 0)
    queue = 0;
  if (queue >= NQUEUE)
    queue = NQUEUE - 1;
  return mlfq_slices[queue];
}

uint64
alloc_enter_seq(void)
{
  acquire(&seq_lock);
  uint64 seq = next_enter_seq++;
  release(&seq_lock);
  return seq;
}

#ifdef MLFQ
// Spec rule 5: a process that gave the cpu up on its own left the queuing
// network, and when it becomes runnable again it goes to the tail of the
// *same* queue it left from -- priority unchanged, just back of the line.
// A fresh ticket is exactly that. p->lock must be held.
static void
mlfq_requeue(struct proc *p)
{
  p->slice_used = 0;
  p->enter_seq = alloc_enter_seq();
}
#endif

#ifdef MLFQ
// Spec rule 7, the anti-starvation boost: every BOOST_INTERVAL ticks every
// process in the system goes back to queue 0, whatever it was doing. Called
// from clockintr() on cpu 0 only, so it fires once per interval rather than
// once per core.
//
// Tickets are left alone on purpose. Everything lands in queue 0 together,
// so relative order among them is decided entirely by the tickets they
// already hold -- which means whoever had been waiting longest still gets
// served first, instead of the boost silently reshuffling the queue.
void
mlfq_boost(void)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->state != UNUSED && p->state != ZOMBIE) {
      p->queue = 0;
      p->slice_used = 0;
    }
    release(&p->lock);
  }
}
#endif

#ifdef MLFQ
// Is anything runnable in a strictly better queue than `queue`? Used to
// honour strict priority at tick boundaries. Must not be called with any
// p->lock held -- the scan takes every one of them in turn, including the
// caller's own.
static int
mlfq_higher_waiting(int queue)
{
  struct proc *p;

  // nothing can outrank queue 0, so skip the scan entirely for the common
  // case of a process that is already at the top.
  if (queue <= 0)
    return 0;

  for (p = proc; p < &proc[NPROC]; p++) {
    // unlocked pre-filter. most of the 64 slots are never RUNNABLE, and
    // taking every p->lock once per tick per cpu is a lot of traffic on the
    // busiest lock in the kernel. a stale read here is harmless: the real
    // check below is done under the lock, and the worst case is missing a
    // preemption for one tick, which the spec explicitly permits.
    if (p->state != RUNNABLE)
      continue;

    acquire(&p->lock);
    int better = (p->state == RUNNABLE && p->queue < queue);
    release(&p->lock);
    if (better)
      return 1;
  }

  return 0;
}
#endif

#ifdef MLFQ
// Called from the timer trap, after update_time() has already charged this
// tick. Returns 1 if the process on this cpu should give up. A process that
// has burnt its whole slice drops one queue -- or, if it is already in the
// bottom queue, just goes to the back of that one, which is what makes
// queue 3 round-robin -- and starts a fresh slice at the tail of wherever it
// lands.
int
mlfq_tick(void)
{
  struct proc *p = myproc();
  int give_up = 0;
  int queue;

  if (p == 0)
    return 0;

  acquire(&p->lock);
  if (p->state != RUNNING) {
    release(&p->lock);
    return 0;
  }
  queue = p->queue;
  if (p->slice_used >= mlfq_slice(queue)) {
    if (p->queue < NQUEUE - 1)
      p->queue++;
    p->slice_used = 0;
    p->enter_seq = alloc_enter_seq();
    give_up = 1;
  }
  release(&p->lock);

  // Strict priority, the other half of spec rule 2: something better turned
  // up while we were running, so step aside at this tick boundary. Note it
  // keeps both its queue and its part-burnt slice -- being interrupted by an
  // unrelated higher-priority arrival is not the process's fault, and the
  // rules only demote a process whose slice is actually spent.
  if (!give_up && mlfq_higher_waiting(queue))
    give_up = 1;

  return give_up;
}
#endif

// Called once per tick from clockintr(). Keeps the running and ready-queue
// totals the report's comparison needs, and burns down the slice of whatever
// is currently on a cpu. Compiled into every build, not just MLFQ, because
// the comparison in 2.2 needs the same numbers out of plain round-robin --
// it only writes bookkeeping fields, so no scheduling decision changes.
// Safe to take p->lock here: acquire() turns interrupts off, so a cpu can
// never be inside clockintr() while already holding one.
void
update_time(void)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++) {
    // unlocked pre-filter, same reasoning as mlfq_higher_waiting(): only
    // RUNNING and RUNNABLE processes are charged anything, and taking all
    // 64 locks every tick contends badly with the scheduler's own scan.
    // Re-checked under the lock below.
    if (p->state != RUNNING && p->state != RUNNABLE)
      continue;

    acquire(&p->lock);
    if (p->state == RUNNING) {
      p->rtime++;
      p->slice_used++;
    } else if (p->state == RUNNABLE) {
      p->wtime++;
    }
    release(&p->lock);
  }
}

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if (pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int)(p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table.
void
procinit(void)
{
  struct proc *p;

  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  initlock(&seq_lock, "enter_seq");
  for (p = proc; p < &proc[NPROC]; p++) {
    initlock(&p->lock, "proc");
    p->state = UNUSED;
    p->kstack = KSTACK((int)(p - proc));
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu *
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc *
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int
allocpid()
{
  int pid;

  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc *
allocproc(void)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;

  // every new process starts at the tail of queue 0, per the mlfq rules.
  p->queue = 0;
  p->slice_used = 0;
  p->enter_seq = alloc_enter_seq();
  p->ctime = ticks;
  p->etime = 0;
  p->rtime = 0;
  p->wtime = 0;
  p->first_run = -1;

  // Allocate a trapframe page.
  if ((p->trapframe = (struct trapframe *)kalloc()) == 0) {
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if (p->pagetable == 0) {
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if (p->trapframe)
    kfree((void *)p->trapframe);
  p->trapframe = 0;
  if (p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
  p->queue = 0;
  p->slice_used = 0;
  p->enter_seq = 0;
  p->ctime = 0;
  p->etime = 0;
  p->rtime = 0;
  p->wtime = 0;
  p->first_run = -1;
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if (pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if (mappages(pagetable, TRAMPOLINE, PGSIZE, (uint64)trampoline,
               PTE_R | PTE_X) < 0) {
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  if (mappages(pagetable, TRAPFRAME, PGSIZE, (uint64)(p->trapframe),
               PTE_R | PTE_W) < 0) {
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;

  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if (n > 0) {
    if (sz + n > TRAPFRAME) {
      return -1;
    }
    if ((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0) {
      return -1;
    }
  } else if (n < 0) {
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
kfork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if ((np = allocproc()) == 0) {
    return -1;
  }

  // Copy user memory from parent to child.
  if (uvmcopy(p->pagetable, np->pagetable, p->sz) < 0) {
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for (i = 0; i < NOFILE; i++)
    if (p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for (pp = proc; pp < &proc[NPROC]; pp++) {
    if (pp->parent == p) {
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
kexit(int status)
{
  struct proc *p = myproc();

  if (p == initproc)
    panic("init exiting");

  // Close all open files.
  for (int fd = 0; fd < NOFILE; fd++) {
    if (p->ofile[fd]) {
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);

  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;
  p->etime = ticks; // an exiting process leaves the queuing system

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
kwait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for (;;) {
    // Scan through table looking for exited children.
    havekids = 0;
    for (pp = proc; pp < &proc[NPROC]; pp++) {
      if (pp->parent == p) {
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        if (pp->state == ZOMBIE) {
          // Found one.
          pid = pp->pid;
          if (addr != 0 &&
              copyout(p->pagetable, p->sz, addr, (char *)&pp->xstate,
                      sizeof(pp->xstate)) < 0) {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          pp->parent = 0;
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // No point waiting if we don't have any children.
    if (!havekids || killed(p)) {
      release(&wait_lock);
      return -1;
    }

    // Wait for a child to exit.
    sleep_prepare(p); //DOC: wait-sleep
    release(&wait_lock);
    sleep();
    acquire(&wait_lock);
  }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();

  c->proc = 0;
  for (;;) {
    // The most recent process to run may have had interrupts
    // turned off; enable them to avoid a deadlock if all
    // processes are waiting. Then turn them back off
    // to avoid a possible race between an interrupt
    // and wfi.
    intr_on();
    intr_off();

#ifdef MLFQ
    // strict priority: the best candidate is the runnable process in the
    // lowest-numbered queue, and among those the one that has been waiting
    // longest, which is the smallest enter_seq. one pass to find it, then a
    // second acquire to actually run it.
    struct proc *best = 0;
    int best_queue = 0;
    uint64 best_seq = 0;

    for (p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if (p->state == RUNNABLE) {
        if (best == 0 || p->queue < best_queue ||
            (p->queue == best_queue && p->enter_seq < best_seq)) {
          best = p;
          best_queue = p->queue;
          best_seq = p->enter_seq;
        }
      }
      release(&p->lock);
    }

    if (best == 0) {
      // nothing to run; stop running on this core until an interrupt.
      asm volatile("wfi");
      continue;
    }

    acquire(&best->lock);
    // another core may have grabbed or killed it while we were scanning, so
    // the state has to be rechecked now that the lock is actually held.
    if (best->state == RUNNABLE) {
      if (best->first_run < 0)
        best->first_run = ticks;
      best->state = RUNNING;
      c->proc = best;
      swtch(&c->context, &best->context);

      // Don't re-enable interrupts on release.
      mycpu()->intena = 0;

      c->proc = 0;
    }
    release(&best->lock);
#else
    int found = 0;
    for (p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if (p->state == RUNNABLE) {
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        if (p->first_run < 0)
          p->first_run = ticks;
        p->state = RUNNING;
        c->proc = p;
        swtch(&c->context, &p->context);

        // Don't re-enable interrupts on release.
        mycpu()->intena = 0;

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
        found = 1;
      }
      release(&p->lock);
    }
    if (found == 0) {
      // nothing to run; stop running on this core until an interrupt.
      asm volatile("wfi");
    }
#endif
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if (!holding(&p->lock))
    panic("sched p->lock");
  if (mycpu()->noff != 1)
    panic("sched locks");
  if (p->state == RUNNING)
    panic("sched RUNNING");
  if (intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  extern char userret[];
  static int first = 1;
  struct proc *p = myproc();

  // Still holding p->lock from scheduler.
  release(&p->lock);

  if (__atomic_load_n(&first, __ATOMIC_ACQUIRE)) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    fsinit(ROOTDEV);

    // ensure other cores see first=0.
    __atomic_store_n(&first, 0, __ATOMIC_RELEASE);

    // We can invoke kexec() now that file system is initialized.
    // Put the return value (argc) of kexec into a0.
    p->trapframe->a0 = kexec("/init", (char *[]){"/init", 0});
    if (p->trapframe->a0 == -1) {
      panic("exec");
    }
  }

  // return to user space, mimicing usertrap()'s return.
  prepare_return();
  uint64 satp = MAKE_SATP(p->pagetable);
  uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64))trampoline_userret)(satp);
}

// Register current process as waiting for wakeups on chan.
void
sleep_prepare(void *chan)
{
  struct proc *p = myproc();

  acquire(&p->lock);
  if (chan == 0)
    panic("sleep_prepare: zero chan");
  p->chan = chan;
  release(&p->lock);
}

// Put the thread to sleep.  Assumes sleep_prepare() was called before.
// If the channel registered by sleep_prepare() has been woken up in
// the meantime, do not go to sleep, and instead return immediately.
void
sleep(void)
{
  struct proc *p = myproc();

  acquire(&p->lock);
  if (p->chan != 0) {
    p->state = SLEEPING;
    sched();
  }
  release(&p->lock);
}

// Wake up all processes sleeping on channel chan.
void
wakeup(void *chan)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->chan == chan) {
      // If the process is waiting for wakeups on this channel,
      // signal that the wakeup happened by clearing p->chan.
      p->chan = 0;

      // If this waiting process has gotten so far as to actually
      // go to sleep, also set it back to RUNNING.
      if (p->state == SLEEPING) {
        p->state = RUNNABLE;
#ifdef MLFQ
        mlfq_requeue(p);
#endif
      }
    }
    release(&p->lock);
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kkill(int pid)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->pid == pid) {
      p->killed = 1;
      if (p->state == SLEEPING) {
        // Wake process from sleep().
        p->state = RUNNABLE;
#ifdef MLFQ
        mlfq_requeue(p);
#endif
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

void
setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int
killed(struct proc *p)
{
  int k;

  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if (user_dst) {
    return copyout(p->pagetable, p->sz, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if (user_src) {
    return copyin(p->pagetable, p->sz, dst, src, len);
  } else {
    memmove(dst, (char *)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
    // clang-format off
    [UNUSED]    = "unused",
    [USED]      = "used",
    [SLEEPING]  = "sleep ",
    [RUNNABLE]  = "runble",
    [RUNNING]   = "run   ",
    [ZOMBIE]    = "zombie"
    // clang-format on
  };
  struct proc *p;
  char *state;

  printk("\n");
  for (p = proc; p < &proc[NPROC]; p++) {
    if (p->state == UNUSED)
      continue;
    if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printk("%d %s %s", p->pid, state, p->name);
#ifdef MLFQ
    // everything needed to check the rules by eye: which queue it sits in,
    // how much of that queue's slice it has burnt, and how long until the
    // next boost drags it back to queue 0.
    printk("  q%d slice %d/%d seq %d  run %d wait %d  boost in %d", p->queue,
           p->slice_used, mlfq_slice(p->queue), (int)p->enter_seq, p->rtime,
           p->wtime, BOOST_INTERVAL - (int)(ticks % BOOST_INTERVAL));
#endif
    printk("\n");
  }
}
