#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/proc_info.h"
#include "user/user.h"

static struct proc_info pinfo[64];

static int
get_proc_prio(int pid)
{
  int n = getpinfo(pinfo, 64);
  for (int i = 0; i < n; i++) {
    if (pinfo[i].pid == pid)
      return pinfo[i].priority;
  }
  return -1;
}

static uint64
get_proc_ticks(int pid)
{
  int n = getpinfo(pinfo, 64);
  for (int i = 0; i < n; i++) {
    if (pinfo[i].pid == pid)
      return pinfo[i].cpu_ticks;
  }
  return 0;
}

static uint
get_proc_sched(int pid)
{
  int n = getpinfo(pinfo, 64);
  for (int i = 0; i < n; i++) {
    if (pinfo[i].pid == pid)
      return pinfo[i].num_sched;
  }
  return 0;
}

static volatile uint64 compute_sink;

// Deterministic compute workload: polynomial arithmetic
static void
do_compute_workload(int iterations)
{
  uint64 sum = 0;
  for (int i = 0; i < iterations; i++) {
    sum += (uint64)i * (uint64)(i ^ 0x5a5a);
  }
  compute_sink = sum;
}

int
main(int argc, char *argv[])
{
  printf("=== MLFQ Test 1: Basic MLFQ Scheduling Test ===\n");

  // Part 1: Initial priority check on a freshly created process
  int init_pid = fork();
  if (init_pid < 0) {
    printf("mlfqtest: fork failed\n");
    exit(1);
  }
  if (init_pid == 0) {
    int p = get_proc_prio(getpid());
    exit(p);
  }
  int child_prio = -1;
  wait(&child_prio);
  printf("mlfqtest: checking initial priority: Q%d\n", child_prio);
  if (child_prio != 0) {
    printf("mlfqtest: FAIL - initial priority is %d, expected 0 (Q0)\n",
           child_prio);
    exit(1);
  }
  printf("mlfqtest: PASS - initial priority is Q0\n");

  // Part 2: Concurrent multi-process scheduling
  printf("mlfqtest: spawning 3 concurrent compute workers...\n");
  int pids[3];
  for (int i = 0; i < 3; i++) {
    int pid = fork();
    if (pid < 0) {
      printf("mlfqtest: fork failed\n");
      exit(1);
    }
    if (pid == 0) {
      // Child performs compute workload
      do_compute_workload(80000000);
      exit(0);
    }
    pids[i] = pid;
  }

  // Parent monitors workers
  pause(5);
  printf("mlfqtest: sampling active worker statistics:\n");
  for (int i = 0; i < 3; i++) {
    printf("  Worker %d (PID %d): prio=Q%d, ticks=%ld, sched=%d\n", i, pids[i],
           get_proc_prio(pids[i]), get_proc_ticks(pids[i]),
           get_proc_sched(pids[i]));
  }

  // Wait for all 3 workers
  for (int i = 0; i < 3; i++) {
    wait(0);
  }
  printf("mlfqtest: all 3 concurrent workers completed.\n");

  // Part 3: CPU-bound demotion verification
  printf("mlfqtest: verifying CPU-bound process demotes to Q1 and Q2...\n");
  int cpid = fork();
  if (cpid < 0) {
    printf("mlfqtest: fork failed\n");
    exit(1);
  }
  if (cpid == 0) {
    do_compute_workload(250000000);
    exit(0);
  }

  int reached_q1 = 0;
  int reached_q2 = 0;
  for (int s = 0; s < 25; s++) {
    pause(1);
    int p = get_proc_prio(cpid);
    if (p == 1)
      reached_q1 = 1;
    if (p == 2)
      reached_q2 = 1;
    if (reached_q2)
      break;
  }
  wait(0);

  printf("mlfqtest: demotion results: reached Q1=%d, reached Q2=%d\n",
         reached_q1, reached_q2);
  if (!reached_q2) {
    printf("mlfqtest: FAIL - compute process did not demote to Q2\n");
    exit(1);
  }
  printf("mlfqtest: PASS - CPU demotion to Q2 verified\n");

  // Part 4: I/O-bound / sleeping priority retention
  printf("mlfqtest: verifying I/O process retains high priority...\n");
  int iopid = fork();
  if (iopid < 0) {
    printf("mlfqtest: fork failed\n");
    exit(1);
  }
  if (iopid == 0) {
    for (int i = 0; i < 15; i++) {
      pause(1);
    }
    exit(0);
  }

  int io_stayed_high = 1;
  for (int s = 0; s < 10; s++) {
    pause(1);
    int p = get_proc_prio(iopid);
    if (p > 1) {
      io_stayed_high = 0;
    }
  }
  wait(0);

  printf("mlfqtest: I/O priority retention: stayed high=%d\n", io_stayed_high);
  if (!io_stayed_high) {
    printf("mlfqtest: FAIL - I/O process dropped to lowest priority\n");
    exit(1);
  }
  printf("mlfqtest: PASS - I/O priority retention verified\n");

  printf("=== MLFQ Test 1: ALL TESTS PASSED ===\n");
  exit(0);
}
