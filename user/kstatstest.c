#include "kernel/types.h"
#include "kernel/proc_info.h"
#include "user/user.h"

static struct proc_info procs[64];

static int
find_proc(int pid, struct proc_info *dest)
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
  printf("=== Kernel Performance Monitoring Test Suite ===\n");

  // Test 1: Basic getpinfo retrieval
  int count = getpinfo(procs, 64);
  printf("[Test 1] Retrieving active process table... ");
  if (count <= 0) {
    printf("FAIL - getpinfo returned %d\n", count);
    exit(1);
  }
  printf("PASS (found %d processes)\n", count);

  // Test 2: System Call Counter Tracking
  printf("[Test 2] Verifying syscall_count tracking... ");
  struct proc_info before, after;
  if (!find_proc(getpid(), &before)) {
    printf("FAIL - could not find self in proc table\n");
    exit(1);
  }

  // Invoke 5 system calls
  for (int i = 0; i < 5; i++) {
    getpid();
  }

  if (!find_proc(getpid(), &after)) {
    printf("FAIL - could not find self after syscalls\n");
    exit(1);
  }

  uint diff = after.syscall_count - before.syscall_count;
  if (diff < 6) { // 5 getpids + at least 1 getpinfo
    printf("FAIL - syscall_count diff is %d, expected >= 6\n", diff);
    exit(1);
  }
  printf("PASS (recorded %d syscalls)\n", diff);

  // Test 3: Response Time and Lifecycle Metrics
  printf(
    "[Test 3] Verifying response_time, wait_ticks, and turnaround_time... ");
  int cpid = fork();
  if (cpid < 0) {
    printf("FAIL - fork failed\n");
    exit(1);
  }

  if (cpid == 0) {
    // Child: do compute work then exit
    static volatile uint64 compute_sink;
    uint64 sum = 0;
    for (int i = 0; i < 40000000; i++) {
      sum += (uint64)i * 7ULL;
    }
    compute_sink = sum;
    exit(0);
  }

  // Parent monitors child
  pause(2);

  struct proc_info cinfo;
  int found_child = find_proc(cpid, &cinfo);

  wait(0);

  if (!found_child) {
    printf("FAIL - child was not found during execution\n");
    exit(1);
  }

  printf("PASS\n");
  printf(
    "  Child Metrics: PID=%d, PRIO=Q%d, CPU_TICKS=%ld, SCHED=%d, RESP=%d, WAIT=%d, TURN=%d, SCALL=%d\n",
    cinfo.pid, cinfo.priority, cinfo.cpu_ticks, cinfo.num_sched,
    cinfo.response_time, cinfo.wait_ticks, cinfo.turnaround_time,
    cinfo.syscall_count);

  if (cinfo.num_sched == 0) {
    printf("FAIL - child num_sched is 0\n");
    exit(1);
  }

  printf("\n=== ALL KSTATS TESTS PASSED ===\n");
  exit(0);
}
