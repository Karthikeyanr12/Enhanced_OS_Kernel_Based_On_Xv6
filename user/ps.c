#include "kernel/types.h"
#include "kernel/proc_info.h"
#include "user/user.h"

static struct proc_info procs[64];

int
main(int argc, char *argv[])
{
  int count = getpinfo(procs, 64);
  if (count < 0) {
    fprintf(2, "ps: error retrieving process info\n");
    exit(1);
  }

  printf("PID    PPID   STATE        PRIO   SIZE        TICKS  SCHED  NAME\n");
  for (int i = 0; i < count; i++) {
    // PID
    printf("%d", procs[i].pid);
    int p = procs[i].pid;
    int spaces = 7;
    do { spaces--; p /= 10; } while (p > 0);
    for (int s = 0; s < spaces; s++) printf(" ");

    // PPID
    printf("%d", procs[i].ppid);
    p = procs[i].ppid;
    spaces = 7;
    do { spaces--; p /= 10; } while (p > 0);
    for (int s = 0; s < spaces; s++) printf(" ");

    // STATE
    printf("%s", procs[i].state);
    int len = strlen(procs[i].state);
    for (int s = 0; s < 13 - len; s++) printf(" ");

    // PRIO
    printf("%d", procs[i].priority);
    int pr = procs[i].priority;
    spaces = 7;
    do { spaces--; pr /= 10; } while (pr > 0);
    for (int s = 0; s < spaces; s++) printf(" ");

    // SIZE
    printf("%ld", procs[i].sz);
    uint64 sz = procs[i].sz;
    spaces = 12;
    do { spaces--; sz /= 10; } while (sz > 0);
    for (int s = 0; s < spaces; s++) printf(" ");

    // TICKS
    printf("%ld", procs[i].cpu_ticks);
    uint64 tk = procs[i].cpu_ticks;
    spaces = 7;
    do { spaces--; tk /= 10; } while (tk > 0);
    for (int s = 0; s < spaces; s++) printf(" ");

    // SCHED
    printf("%d", procs[i].num_sched);
    int sc = procs[i].num_sched;
    spaces = 7;
    do { spaces--; sc /= 10; } while (sc > 0);
    for (int s = 0; s < spaces; s++) printf(" ");

    // NAME
    printf("%s\n", procs[i].name);
  }

  exit(0);
}
