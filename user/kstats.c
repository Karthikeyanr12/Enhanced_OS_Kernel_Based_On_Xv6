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
  if (v < 0) v = -v;
  int digits = 0;
  do { digits++; v /= 10; } while (v > 0);
  if (val < 0) digits++;
  print_spaces(width - digits);
}

int
main(int argc, char *argv[])
{
  int count = getpinfo(procs, 64);
  if (count < 0) {
    fprintf(2, "kstats: error retrieving process info\n");
    exit(1);
  }

  printf("PID    PPID   STATE        PRIO   TICKS  SCHED  RESP   WAIT   TURN   SCALL  NAME\n");
  for (int i = 0; i < count; i++) {
    // PID
    print_num_col(procs[i].pid, 7);

    // PPID
    print_num_col(procs[i].ppid, 7);

    // STATE
    printf("%s", procs[i].state);
    int len = strlen(procs[i].state);
    print_spaces(13 - len);

    // PRIO
    printf("Q%d", procs[i].priority);
    print_spaces(5);

    // TICKS
    print_num_col(procs[i].cpu_ticks, 7);

    // SCHED
    print_num_col(procs[i].num_sched, 7);

    // RESP
    print_num_col(procs[i].response_time, 7);

    // WAIT
    print_num_col(procs[i].wait_ticks, 7);

    // TURN
    print_num_col(procs[i].turnaround_time, 7);

    // SCALL
    print_num_col(procs[i].syscall_count, 7);

    // NAME
    printf("%s\n", procs[i].name);
  }

  exit(0);
}
