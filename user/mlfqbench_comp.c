#include "kernel/types.h"
#include "kernel/proc_info.h"
#include "user/user.h"

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
  if (v < 0)
    v = -v;
  int digits = 0;
  do {
    digits++;
    v /= 10;
  } while (v > 0);
  if (val < 0)
    digits++;
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

static volatile uint64 compute_sink;

int
main(int argc, char *argv[])
{
  printf("=== Benchmark 4: Competing Workloads Benchmark (MLFQ) ===\n");
  printf("Launching 4 concurrent competing processes:\n");
  printf("  - 2 CPU-bound processes\n");
  printf("  - 1 I/O-bound process\n");
  printf("  - 1 Mixed-workload process\n\n");

  int pids[4];
  const char *types[4] = {"CPU-1", "CPU-2", "IO-1 ", "MIX-1"};
  const char *csv_types[4] = {"COMP_CPU1", "COMP_CPU2", "COMP_IO1",
                              "COMP_MIX1"};
  struct proc_info final_stats[4];

  // Child 0: CPU-bound 1
  pids[0] = fork();
  if (pids[0] == 0) {
    uint64 s = 0;
    for (int i = 0; i < 100000000; i++)
      s += (uint64)i * 11ULL;
    compute_sink = s;
    exit(0);
  }

  // Child 1: CPU-bound 2
  pids[1] = fork();
  if (pids[1] == 0) {
    uint64 s = 0;
    for (int i = 0; i < 100000000; i++)
      s += (uint64)i * 13ULL;
    compute_sink = s;
    exit(0);
  }

  // Child 2: I/O-bound
  pids[2] = fork();
  if (pids[2] == 0) {
    for (int i = 0; i < 8; i++) {
      uint64 s = 0;
      for (int k = 0; k < 20000; k++)
        s += (uint64)k;
      compute_sink = s;
      pause(1);
    }
    exit(0);
  }

  // Child 3: Mixed
  pids[3] = fork();
  if (pids[3] == 0) {
    for (int i = 0; i < 5; i++) {
      uint64 s = 0;
      for (int k = 0; k < 25000000; k++)
        s += (uint64)k * 5ULL;
      compute_sink = s;
      pause(1);
      getpid();
    }
    exit(0);
  }

  // Poll until all workers transition to ZOMBIE (finished execution)
  for (;;) {
    int all_done = 1;
    for (int i = 0; i < 4; i++) {
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
  for (int i = 0; i < 4; i++) {
    wait(0);
  }

  printf("\n--- Multi-Process Competing Benchmark Results ---\n");
  printf("TYPE    PID    PRIO   TICKS  SCHED  RESP   WAIT   TURN   SCALL\n");
  for (int i = 0; i < 4; i++) {
    print_str_col(types[i], 8);
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

    printf("CSV: %s,%d,%d,%ld,%d,%d,%d,%d,%d\n", csv_types[i],
           final_stats[i].pid, final_stats[i].priority,
           final_stats[i].cpu_ticks, final_stats[i].num_sched,
           final_stats[i].response_time, final_stats[i].wait_ticks,
           final_stats[i].turnaround_time, final_stats[i].syscall_count);
  }

  printf("=== Benchmark 4: Completed ===\n");
  exit(0);
}
