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
};

#endif
