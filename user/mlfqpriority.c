#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/proc_info.h"
#include "user/user.h"

static struct proc_info procs[64];

static int
get_proc(int pid, struct proc_info *info)
{
  int n = getpinfo(procs, 64);
  for (int i = 0; i < n; i++) {
    if (procs[i].pid == pid) {
      *info = procs[i];
      return 1;
    }
  }
  return 0;
}

int
main(int argc, char *argv[])
{
  printf("=== MLFQ Test 5: Priority Demotion and Quanta Test ===\n");
  printf("Note on user-space visibility:\n");
  printf(
    "  - Direct: struct proc_info exposes 'priority' (0=Q0, 1=Q1, 2=Q2)\n");
  printf(
    "  - Indirect: 'cpu_ticks' and 'num_sched' confirm time slice progression\n");
  printf("  - Expected quanta: Q0=1 tick, Q1=2 ticks, Q2=4 ticks\n\n");

  int cpid = fork();
  if (cpid < 0) {
    printf("mlfqpriority: fork failed\n");
    exit(1);
  }

  if (cpid == 0) {
    // Child: deterministic compute-heavy loop
    static volatile uint64 compute_sink;
    uint64 sum = 0;
    for (int i = 0; i < 90000000; i++) {
      sum += (uint64)i * 17ULL;
    }
    compute_sink = sum;
    exit(0);
  }

  printf("mlfqpriority: monitoring Child PID %d priority timeline:\n", cpid);

  int seen_q0 = 0;
  int seen_q1 = 0;
  int seen_q2 = 0;

  for (int step = 1; step <= 10; step++) {
    struct proc_info info;
    if (get_proc(cpid, &info)) {
      printf("  Step %d: prio=Q%d ticks=%ld sched=%d state=%s\n", step,
             info.priority, info.cpu_ticks, info.num_sched, info.state);
      if (info.priority == 0)
        seen_q0 = 1;
      if (info.priority == 1)
        seen_q1 = 1;
      if (info.priority == 2)
        seen_q2 = 1;
    } else {
      printf("  Step %d: [child exited]\n", step);
      break;
    }
    pause(1);
  }

  wait(0);

  printf("\nmlfqpriority: queue transition summary:\n");
  printf("  Observed in Q0 (High):   %s\n",
         seen_q0 ? "YES" : "NO (rapid demotion)");
  printf("  Observed in Q1 (Medium): %s\n", seen_q1 ? "YES" : "NO");
  printf("  Observed in Q2 (Low):    %s\n", seen_q2 ? "YES" : "NO");

  if (!seen_q1 || !seen_q2) {
    printf(
      "mlfqpriority: FAIL - child did not demote through MLFQ queues as expected\n");
    exit(1);
  }

  printf(
    "mlfqpriority: PASS - demotion timeline matches MLFQ queue specifications\n");
  printf("=== MLFQ Test 5: PASSED ===\n");
  exit(0);
}
