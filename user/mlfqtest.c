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

int
main(int argc, char *argv[])
{
  printf("mlfqtest: starting MLFQ validation tests...\n");

  // Test 1: Check initial process priority is Q0 (0)
  int my_prio = get_proc_prio(getpid());
  printf("mlfqtest: initial process priority is Q%d\n", my_prio);
  if (my_prio != 0) {
    printf("mlfqtest: FAIL - initial priority should be 0\n");
    exit(1);
  }

  // Test 2: CPU-bound process should demote to Q1 and Q2
  printf("mlfqtest: testing CPU-bound process demotion...\n");
  int cpid = fork();
  if (cpid < 0) {
    printf("mlfqtest: fork failed\n");
    exit(1);
  }

  if (cpid == 0) {
    // Child: do heavy computation
    volatile uint64 count = 0;
    while (count < 2000000000ULL) {
      count++;
    }
    exit(0);
  }

  // Parent monitors child priority
  int reached_q1 = 0;
  int reached_q2 = 0;
  for (int s = 0; s < 30; s++) {
    pause(1);
    int p = get_proc_prio(cpid);
    if (p == 1) reached_q1 = 1;
    if (p == 2) reached_q2 = 1;
    if (reached_q2) break;
  }

  printf("mlfqtest: reached Q1=%d, reached Q2=%d\n", reached_q1, reached_q2);
  if (!reached_q2) {
    printf("mlfqtest: FAIL - compute process did not demote to Q2\n");
    kill(cpid);
    wait(0);
    exit(1);
  }

  // Test 3: I/O-bound process should retain high priority (Q0/Q1)
  printf("mlfqtest: testing I/O-bound process priority retention...\n");
  int iopid = fork();
  if (iopid < 0) {
    printf("mlfqtest: fork failed\n");
    kill(cpid);
    wait(0);
    exit(1);
  }

  if (iopid == 0) {
    for (int i = 0; i < 20; i++) {
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

  printf("mlfqtest: I/O process stayed at high priority=%d\n", io_stayed_high);
  if (!io_stayed_high) {
    printf("mlfqtest: FAIL - I/O process dropped to low priority\n");
    kill(cpid);
    kill(iopid);
    wait(0);
    wait(0);
    exit(1);
  }

  kill(cpid);
  wait(0);
  wait(0);

  printf("mlfqtest: ALL MLFQ TESTS PASSED!\n");
  exit(0);
}
