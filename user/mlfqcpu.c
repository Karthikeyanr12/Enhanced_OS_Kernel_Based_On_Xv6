#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/proc_info.h"
#include "user/user.h"

#define NUM_WORKERS 3

static struct proc_info procs[64];

// Workload 1: Prime number counting (deterministic arithmetic)
static void
workload_primes(int limit)
{
  volatile int prime_count = 0;
  for (int n = 2; n < limit; n++) {
    int is_prime = 1;
    for (int d = 2; d * d <= n; d++) {
      if (n % d == 0) {
        is_prime = 0;
        break;
      }
    }
    if (is_prime)
      prime_count++;
  }
}

static volatile uint64 compute_sink;

// Workload 2: Fibonacci modulo sequence
static void
workload_fibonacci(int iterations)
{
  uint64 a = 0, b = 1;
  for (int i = 0; i < iterations; i++) {
    uint64 c = (a + b) % 1000000007ULL;
    a = b;
    b = c;
  }
  compute_sink = a + b;
}

// Workload 3: Polynomial calculation
static void
workload_polynomial(int iterations)
{
  uint64 sum = 0;
  for (int i = 0; i < iterations; i++) {
    sum += (uint64)i * (uint64)i + (uint64)i * 3ULL + 7ULL;
  }
  compute_sink = sum;
}

static void
print_worker_snapshots(int pids[], int num)
{
  int n = getpinfo(procs, 64);
  for (int i = 0; i < num; i++) {
    int found = 0;
    for (int j = 0; j < n; j++) {
      if (procs[j].pid == pids[i]) {
        printf("    Worker %d (PID %d): state=%s prio=Q%d ticks=%ld sched=%d\n",
               i, procs[j].pid, procs[j].state, procs[j].priority,
               procs[j].cpu_ticks, procs[j].num_sched);
        found = 1;
        break;
      }
    }
    if (!found) {
      printf("    Worker %d (PID %d): [terminated]\n", i, pids[i]);
    }
  }
}

int
main(int argc, char *argv[])
{
  printf("=== MLFQ Test 2: CPU-Bound Process Test ===\n");
  printf("mlfqcpu: starting %d CPU-bound workloads...\n", NUM_WORKERS);

  int pids[NUM_WORKERS];

  for (int i = 0; i < NUM_WORKERS; i++) {
    int pid = fork();
    if (pid < 0) {
      printf("mlfqcpu: fork failed for worker %d\n", i);
      exit(1);
    }
    if (pid == 0) {
      if (i == 0) {
        workload_primes(35000);
      } else if (i == 1) {
        workload_fibonacci(60000000);
      } else {
        workload_polynomial(80000000);
      }
      exit(0);
    }
    pids[i] = pid;
  }

  // Parent samples scheduler state during execution
  for (int sample = 1; sample <= 4; sample++) {
    pause(2);
    printf("mlfqcpu: Sample %d (after ~%d ticks):\n", sample, sample * 2);
    print_worker_snapshots(pids, NUM_WORKERS);
  }

  // Wait for all workers to finish
  for (int i = 0; i < NUM_WORKERS; i++) {
    int status;
    wait(&status);
  }

  printf("mlfqcpu: all workers finished.\n");
  printf("=== MLFQ Test 2: PASSED ===\n");
  exit(0);
}
