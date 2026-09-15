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
  printf("=== MLFQ Test 6: Starvation Prevention / Priority Boost Test ===\n");
  printf("mlfqstarvation: verifying periodic priority boost (interval = 100 ticks)\n");

  int t_start = uptime();
  int next_boost = ((t_start / 100) + 1) * 100;
  int ticks_to_boost = next_boost - t_start;

  printf("mlfqstarvation: current uptime = %d ticks. Next boost at tick %d (~%d ticks remaining)\n",
         t_start, next_boost, ticks_to_boost);

  int cpid = fork();
  if (cpid < 0) {
    printf("mlfqstarvation: fork failed\n");
    exit(1);
  }

  if (cpid == 0) {
    // Child: compute loop to cross the boost boundary
    volatile uint64 sum = 0;
    for (int i = 0; i < 350000000; i++) {
      sum += (uint64)i * 19ULL;
    }
    exit(0);
  }

  // Parent monitors child priority across the boost interval
  int reached_q2 = 0;
  int boost_detected = 0;
  int max_samples = (ticks_to_boost / 3) + 10;
  if (max_samples < 15) max_samples = 15;

  printf("mlfqstarvation: monitoring Child PID %d priority...\n", cpid);
  for (int s = 0; s < max_samples; s++) {
    pause(3);
    int now = uptime();
    struct proc_info info;
    if (get_proc(cpid, &info)) {
      printf("  Tick %d: Child PID %d prio=Q%d ticks=%ld sched=%d state=%s\n",
             now, info.pid, info.priority, info.cpu_ticks, info.num_sched, info.state);

      if (info.priority == 2) {
        reached_q2 = 1;
      }

      // If child reached Q2 earlier and now we are at or past the boost tick,
      // check if priority was reset to Q0 (or Q1 during demotion).
      if (reached_q2 && now >= next_boost) {
        if (info.priority < 2) {
          boost_detected = 1;
          printf("mlfqstarvation: Priority boost detected! Child priority raised from Q2 -> Q%d at tick %d\n",
                 info.priority, now);
          break;
        }
      }
    } else {
      // Child already finished
      break;
    }
  }

  wait(0);

  printf("\nmlfqstarvation: Starvation prevention assessment:\n");
  printf("  1. Child successfully demoted to Q2: %s\n", reached_q2 ? "YES (PASS)" : "NO (FAIL)");
  printf("  2. Priority boost reset Q2 -> Q0/Q1:  %s\n", boost_detected ? "YES (PASS)" : "NO (FAIL)");

  if (reached_q2 && boost_detected) {
    printf("mlfqstarvation: PASS - Starvation prevention mechanism confirmed working.\n");
    printf("=== MLFQ Test 6: PASSED ===\n");
    exit(0);
  } else {
    printf("mlfqstarvation: FAIL - Starvation prevention boost was not detected.\n");
    exit(1);
  }
}

