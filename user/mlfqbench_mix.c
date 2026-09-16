#include "kernel/types.h"
#include "kernel/proc_info.h"
#include "user/user.h"

#define NUM_MIXED_WORKERS 2

static struct proc_info procs[64];

static void
print_spaces(int n)
{
  for (int i = 0; i < n; i++)
    printf(" ");
}

static void
print_num_col(long val, int width)
{
  printf("%ld", val);
  long v = val;
  if (v < 0) v = -v;
  int digits = 0;
  do { digits++; v /= 10; } while (v > 0);
  if (val < 0) digits++;
  print_spaces(width - digits);
}

static void
print_str_col(const char *s, int width)
{
  printf("%s", s);
  int len = strlen(s);
  print_spaces(width - len);
}

static int
get_info(int pid, struct proc_info *dest)
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
  printf("=== Benchmark 3: Mixed Workload (MLFQ) ===\n");
  printf("Spawning %d mixed-workload workers...\n", NUM_MIXED_WORKERS);

  int pids[NUM_MIXED_WORKERS];
  struct proc_info final_stats[NUM_MIXED_WORKERS];

  for (int i = 0; i < NUM_MIXED_WORKERS; i++) {
    int pid = fork();
    if (pid < 0) {
      printf("fork failed\n");
      exit(1);
    }
    if (pid == 0) {
      // Mixed: compute bursts separated by sleep and system calls
      for (int cycle = 0; cycle < 5; cycle++) {
        volatile uint64 s = 0;
        for (int k = 0; k < 18000000; k++) {
          s += (uint64)k * 3ULL;
        }
        pause(1);
        getpid();
        uptime();
      }
      exit(0);
    }
    pids[i] = pid;
  }

  // Poll until all workers transition to ZOMBIE (finished execution)
  for (;;) {
    int all_done = 1;
    for (int i = 0; i < NUM_MIXED_WORKERS; i++) {
      struct proc_info info;
      if (get_info(pids[i], &info)) {
        if (strcmp(info.state, "ZOMBIE") != 0) {
          all_done = 0;
        } else {
          final_stats[i] = info;
        }
      }
    }
    if (all_done)
      break;
    pause(1);
  }

  // Reap child processes
  for (int i = 0; i < NUM_MIXED_WORKERS; i++) {
    wait(0);
  }

  printf("\n--- Mixed Workload Benchmark Results ---\n");
  printf("WORKER  PID    PRIO   TICKS  SCHED  RESP   WAIT   TURN   SCALL\n");
  for (int i = 0; i < NUM_MIXED_WORKERS; i++) {
    char wname[16];
    wname[0] = 'M'; wname[1] = 'I'; wname[2] = 'X'; wname[3] = '-';
    wname[4] = '0' + i; wname[5] = '\0';
    print_str_col(wname, 8);
    print_num_col(final_stats[i].pid, 7);
    printf("Q%d", final_stats[i].priority);
    print_spaces(5);
    print_num_col(final_stats[i].cpu_ticks, 7);
    print_num_col(final_stats[i].num_sched, 7);
    print_num_col(final_stats[i].response_time, 7);
    print_num_col(final_stats[i].wait_ticks, 7);
    print_num_col(final_stats[i].turnaround_time, 7);
    print_num_col(final_stats[i].syscall_count, 7);
    printf("\n");

    printf("CSV: MIX,%d,%d,%ld,%d,%d,%d,%d,%d\n",
           final_stats[i].pid, final_stats[i].priority,
           final_stats[i].cpu_ticks, final_stats[i].num_sched,
           final_stats[i].response_time, final_stats[i].wait_ticks,
           final_stats[i].turnaround_time, final_stats[i].syscall_count);
  }

  printf("=== Benchmark 3: Completed ===\n");
  exit(0);
}
