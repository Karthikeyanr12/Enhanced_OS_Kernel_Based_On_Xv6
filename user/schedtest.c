#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/proc_info.h"
#include "user/user.h"

static struct proc_info procs[64];

static void
print_stats_table(int count)
{
  printf("--- Active Process Table ---\n");
  for (int i = 0; i < count; i++) {
    printf(
      "PID=%d PPID=%d STATE=%s PRIO=Q%d SIZE=%ld TICKS=%ld SCHED=%d NAME=%s\n",
      procs[i].pid, procs[i].ppid, procs[i].state, procs[i].priority,
      procs[i].sz, procs[i].cpu_ticks, procs[i].num_sched, procs[i].name);
  }
}

int
main(int argc, char *argv[])
{
  printf("=== MLFQ Test 4: Scheduler Statistics Test ===\n");

  int parent_pid = getpid();
  int c1 = fork();
  if (c1 < 0) {
    printf("schedtest: fork c1 failed\n");
    exit(1);
  }
  if (c1 == 0) {
    // Child 1: compute workload to accumulate ticks and schedules
    static volatile uint64 compute_sink;
    uint64 sum = 0;
    for (int i = 0; i < 60000000; i++) {
      sum += (uint64)i * 3ULL;
    }
    compute_sink = sum;
    exit(0);
  }

  int c2 = fork();
  if (c2 < 0) {
    printf("schedtest: fork c2 failed\n");
    kill(c1);
    wait(0);
    exit(1);
  }
  if (c2 == 0) {
    // Child 2: sleeping to observe SLEEPING state
    pause(4);
    exit(0);
  }

  // Let them run for a moment
  pause(2);

  int count = getpinfo(procs, 64);
  if (count <= 0) {
    printf("schedtest: FAIL - getpinfo returned %d\n", count);
    kill(c1);
    kill(c2);
    wait(0);
    wait(0);
    exit(1);
  }

  printf("schedtest: current process table from getpinfo():\n");
  print_stats_table(count);

  // Validate statistics for child 1 and child 2
  int found_c1 = 0, found_c2 = 0;
  int ppid_ok = 1;
  int ticks_ok = 0;
  int sched_ok = 0;
  int state_ok = 0;

  for (int i = 0; i < count; i++) {
    if (procs[i].pid == c1) {
      found_c1 = 1;
      if (procs[i].ppid != parent_pid)
        ppid_ok = 0;
      if (procs[i].cpu_ticks > 0)
        ticks_ok = 1;
      if (procs[i].num_sched > 0)
        sched_ok = 1;
    }
    if (procs[i].pid == c2) {
      found_c2 = 1;
      if (procs[i].ppid != parent_pid)
        ppid_ok = 0;
      if (strcmp(procs[i].state, "SLEEPING") == 0 ||
          strcmp(procs[i].state, "RUNNABLE") == 0) {
        state_ok = 1;
      }
    }
  }

  // Wait for children to complete
  wait(0);
  wait(0);

  printf("schedtest: validation checklist:\n");
  printf("  1. Found active children in table: %s\n",
         (found_c1 && found_c2) ? "PASS" : "FAIL");
  printf("  2. PPID correctly matches parent (%d): %s\n", parent_pid,
         ppid_ok ? "PASS" : "FAIL");
  printf("  3. cpu_ticks increased with execution: %s\n",
         ticks_ok ? "PASS" : "FAIL");
  printf("  4. num_sched increased with dispatch: %s\n",
         sched_ok ? "PASS" : "FAIL");
  printf("  5. Sleeping child state reported correctly: %s\n",
         state_ok ? "PASS" : "FAIL");

  if (!found_c1 || !found_c2 || !ppid_ok || !ticks_ok || !sched_ok ||
      !state_ok) {
    printf("schedtest: FAIL - statistics verification failed\n");
    exit(1);
  }

  printf("=== MLFQ Test 4: PASSED ===\n");
  exit(0);
}
