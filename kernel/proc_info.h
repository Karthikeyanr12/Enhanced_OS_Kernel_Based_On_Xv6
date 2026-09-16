#ifndef _PROC_INFO_H_
#define _PROC_INFO_H_

#include "types.h"

struct proc_info {
  int pid;            // Process ID
  int ppid;           // Parent Process ID
  char state[16];     // State name ("RUNNING", "SLEEPING", etc.)
  uint64 sz;          // Process memory size in bytes
  char name[16];      // Process name
  uint64 cpu_ticks;   // Accumulated CPU ticks
  uint num_sched;     // Number of times scheduled
  int priority;       // Current MLFQ queue (0, 1, 2)
  uint ctime;         // Creation time (ticks)
  uint response_time; // Response time (ticks to first execution)
  uint wait_ticks;    // Total ticks spent waiting in RUNNABLE state
  uint turnaround_time; // Total ticks from creation to exit (or elapsed)
  uint syscall_count; // Total system calls executed
};

#endif
