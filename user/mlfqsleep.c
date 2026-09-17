#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/proc_info.h"
#include "user/user.h"

static struct proc_info procs[64];

static int
find_proc_info(int pid, struct proc_info *dest)
{
  int n = getpinfo(procs, 64);
  for (int i = 0; i < n; i++) {
    if (procs[i].pid == pid) {
      *dest = procs[i];
      return 1;
    }
  }
  return 0;
}

int
main(int argc, char *argv[])
{
  printf("=== MLFQ Test 3: Sleep vs CPU-Bound Test ===\n");

  // Fork CPU-bound worker
  int cpu_pid = fork();
  if (cpu_pid < 0) {
    printf("mlfqsleep: fork cpu failed\n");
    exit(1);
  }
  if (cpu_pid == 0) {
    static volatile uint64 compute_sink;
    uint64 sum = 0;
    for (int i = 0; i < 90000000; i++) {
      sum += (uint64)i * 13ULL;
    }
    compute_sink = sum;
    exit(0);
  }

  // Fork Sleeping / Interactive worker
  int sleep_pid = fork();
  if (sleep_pid < 0) {
    printf("mlfqsleep: fork sleep failed\n");
    kill(cpu_pid);
    wait(0);
    exit(1);
  }
  if (sleep_pid == 0) {
    static volatile uint64 compute_sink;
    for (int cycle = 0; cycle < 12; cycle++) {
      // Small compute workload
      uint64 sum = 0;
      for (int i = 0; i < 80000; i++) {
        sum += (uint64)i;
      }
      compute_sink = sum;
      pause(1); // sleep for 1 tick
    }
    exit(0);
  }

  printf(
    "mlfqsleep: spawned CPU-bound worker (PID %d) and Sleeping worker (PID %d)\n",
    cpu_pid, sleep_pid);

  int cpu_demoted_to_q2 = 0;
  int sleep_retained_high = 1;

  for (int sample = 1; sample <= 5; sample++) {
    pause(2);
    struct proc_info info_cpu, info_sleep;
    int found_cpu = find_proc_info(cpu_pid, &info_cpu);
    int found_sleep = find_proc_info(sleep_pid, &info_sleep);

    printf("Sample %d:\n", sample);
    if (found_cpu) {
      printf("  CPU-bound  (PID %d): prio=Q%d, state=%s, ticks=%ld, sched=%d\n",
             info_cpu.pid, info_cpu.priority, info_cpu.state,
             info_cpu.cpu_ticks, info_cpu.num_sched);
      if (info_cpu.priority >= 2)
        cpu_demoted_to_q2 = 1;
    }
    if (found_sleep) {
      printf("  Sleeping   (PID %d): prio=Q%d, state=%s, ticks=%ld, sched=%d\n",
             info_sleep.pid, info_sleep.priority, info_sleep.state,
             info_sleep.cpu_ticks, info_sleep.num_sched);
      if (info_sleep.priority > 1)
        sleep_retained_high = 0;
    }
  }

  // Wait for both children
  wait(0);
  wait(0);

  printf("mlfqsleep: verification results:\n");
  printf("  CPU-bound demoted to Q2: %s\n",
         cpu_demoted_to_q2 ? "YES (PASSED)" : "NO");
  printf("  Sleeping process retained high priority (Q0/Q1): %s\n",
         sleep_retained_high ? "YES (PASSED)" : "NO");

  if (!cpu_demoted_to_q2 || !sleep_retained_high) {
    printf(
      "mlfqsleep: FAIL - scheduler behavior did not differentiate CPU and I/O tasks\n");
    exit(1);
  }

  printf("=== MLFQ Test 3: PASSED ===\n");
  exit(0);
}
