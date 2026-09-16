# Enhanced Operating System Kernel Based on xv6
## Feature 4: Kernel Performance Monitoring and Benchmarking Report

---

### Executive Summary

As part of the academic operating system project **"Enhanced Operating System Kernel Based on xv6"**, **Feature 4: Kernel Performance Monitoring and Benchmarking** provides complete kernel-level instrumentation to quantitatively observe, record, and evaluate process execution dynamics, Multi-Level Feedback Queue (MLFQ) scheduling decisions, and system call activity.

All metrics are gathered with low overhead in the kernel, exposed seamlessly through the extended `getpinfo()` system call, visualized via the user-space `kstats` utility, and benchmarked using four dedicated test suites (`mlfqbench_cpu`, `mlfqbench_io`, `mlfqbench_mix`, `mlfqbench_comp`).

---

### 1. Performance Metrics & Mathematical Definitions

The kernel tracks five primary performance metrics for every active and terminating process:

| Metric | Symbol / Formula | Kernel Field | Description |
| :--- | :--- | :--- | :--- |
| **CPU Time** | $T_{\text{cpu}}$ | `p->cpu_ticks` | Total clock ticks spent by the process executing on the CPU (incremented during timer interrupts). |
| **Schedule Count** | $N_{\text{sched}}$ | `p->num_sched` | Total number of times the scheduler dispatched the process to run on a CPU hart. |
| **Response Time** | $T_{\text{resp}} = T_{\text{first\_sched}} - T_{\text{creation}}$ | `p->first_sched_time - p->ctime` | Number of ticks elapsed between process creation (`allocproc`) and its very first CPU dispatch. |
| **Waiting Time** | $T_{\text{wait}} = \sum \Delta T_{\text{RUNNABLE}}$ | `p->wait_ticks` | Cumulative ticks spent waiting in the `RUNNABLE` state ready for CPU allocation. |
| **Turnaround Time** | $T_{\text{turn}} = T_{\text{exit}} - T_{\text{creation}}$ | `p->etime - p->ctime` | Total elapsed lifetime from process creation until termination (`kexit`). |
| **System Call Count** | $N_{\text{syscall}}$ | `p->syscall_count` | Total number of system calls invoked by the process via `ecall`. |
| **MLFQ Priority** | $Q \in \{0, 1, 2\}$ | `p->priority` | Current active or final queue priority within the 3-level MLFQ scheduler. |

---

### 2. Kernel Instrumentation Architecture

#### 2.1 Process Table Extensions (`kernel/proc.h`)
```c
struct proc {
  struct spinlock lock;
  ...
  // Performance Monitoring Fields (Feature 4)
  uint ctime;             // Creation tick timestamp
  uint first_sched_time;   // First dispatch tick timestamp
  uint etime;             // Exit tick timestamp
  uint wait_ticks;        // Cumulative ticks in RUNNABLE state
  uint ready_start;       // Tick timestamp when entered RUNNABLE
  uint syscall_count;     // Total syscalls invoked
};
```

#### 2.2 User-Facing Interface (`kernel/proc_info.h`)
```c
struct proc_info {
  int pid;
  int ppid;
  char state[16];
  uint64 sz;
  char name[16];
  uint64 cpu_ticks;
  uint num_sched;
  int priority;
  uint ctime;
  uint response_time;
  uint wait_ticks;
  uint turnaround_time;
  uint syscall_count;
};
```

#### 2.3 Kernel Lifecycle Hooks
1. **Creation (`allocproc`)**:
   - Initializes `ctime = ticks`, `first_sched_time = 0`, `etime = 0`, `wait_ticks = 0`, `syscall_count = 0`.
2. **Entering RUNNABLE (`userinit`, `kfork`, `yield`, `wakeup`, `kkill`)**:
   - Records `ready_start = ticks` whenever state transitions to `RUNNABLE`.
3. **Dispatch (`scheduler`)**:
   - On first dispatch (`num_sched == 0`), records `first_sched_time = ticks`.
   - Accumulates waiting duration: `wait_ticks += (ticks - ready_start)`.
4. **Termination (`kexit`)**:
   - Records `etime = ticks` upon entering `ZOMBIE` state.
5. **System Call Dispatcher (`syscall`)**:
   - Atomically increments `p->syscall_count++` on every system call trap.
6. **Information Retrieval (`proc_getpinfo`)**:
   - Populates `response_time = (p->num_sched > 0) ? (p->first_sched_time - p->ctime) : 0`.
   - Populates `turnaround_time = (p->state == ZOMBIE) ? (p->etime - p->ctime) : (ticks - p->ctime)`.
   - Supports both running processes and zombie processes before parent reaping.

#### 2.4 Deadlock Avoidance and Lock Hierarchy
- In xv6, timer interrupt handler `clockintr()` acquires `tickslock` before waking sleeping processes (which acquire `p->lock`). Thus, the lock ordering hierarchy is:
  $$\text{tickslock} \longrightarrow \text{p->lock}$$
- To eliminate any possibility of lock inversion or deadlock, `proc.c` and `scheduler()` read the 32-bit `ticks` variable directly (atomic load on 64-bit RISC-V) without acquiring `tickslock` while holding `p->lock`.

---

### 3. Experimental Benchmark Workloads

Four specialized benchmark utilities were implemented to evaluate scheduling dynamics:

1. **`mlfqbench_cpu`**: Compute-heavy workload executing 120,000,000 polynomial operations. Tests priority demotion from $Q_0 \to Q_1 \to Q_2$.
2. **`mlfqbench_io`**: Interactive I/O-bound simulation performing brief computation followed by voluntary sleep (`pause(1)`). Tests high-priority retention in $Q_0$.
3. **`mlfqbench_mix`**: Mixed workload alternating between compute bursts and I/O sleeps/syscalls. Evaluates dynamic priority adjustments.
4. **`mlfqbench_comp`**: Multi-process competitive environment running 2 CPU-bound, 1 I/O-bound, and 1 mixed process concurrently. Evaluates fairness, latency isolation, and scheduling interference.

---

### 4. Measured Experimental Results

All benchmark workloads were executed inside the xv6-riscv QEMU environment. The parsed results generated by `scripts/parse_benchmark.py` are presented below:

#### 4.1 Structured Benchmark Results Table

| Workload Type | PID | Final Priority | CPU Ticks | Schedules | Response Time | Waiting Time | Turnaround Time | Syscalls |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **CPU-0 (Compute)** | 4 | **Q2** | 6 | 3 | 0 ticks | 0 ticks | 6 ticks | 1 |
| **CPU-1 (Compute)** | 5 | **Q2** | 5 | 3 | 0 ticks | 0 ticks | 6 ticks | 1 |
| **IO-0 (Interactive)** | 4 | **Q0** | 0 | 9 | 0 ticks | 0 ticks | 8 ticks | 9 |
| **IO-1 (Interactive)** | 5 | **Q0** | 0 | 9 | 0 ticks | 0 ticks | 8 ticks | 9 |
| **MIX-0 (Mixed)** | 4 | **Q2** | 3 | 9 | 0 ticks | 0 ticks | 8 ticks | 16 |
| **MIX-1 (Mixed)** | 5 | **Q0** | 0 | 6 | 0 ticks | 0 ticks | 6 ticks | 16 |
| **COMP_CPU1 (Contention)**| 4 | **Q2** | 5 | 5 | 0 ticks | 0 ticks | 5 ticks | 1 |
| **COMP_CPU2 (Contention)**| 5 | **Q2** | 4 | 3 | 0 ticks | 0 ticks | 5 ticks | 1 |
| **COMP_IO1 (Contention)** | 6 | **Q0** | 0 | 9 | 1 ticks | 1 ticks | 9 ticks | 9 |
| **COMP_MIX1 (Contention)**| 7 | **Q1** | 4 | 10 | 1 ticks | 1 ticks | 10 ticks | 11 |

#### 4.2 Raw CSV Data
```csv
Type,PID,Priority,CPU_Ticks,Num_Sched,Response_Time,Wait_Time,Turnaround_Time,Syscalls
CPU,4,Q2,6,3,0,0,6,1
CPU,5,Q2,5,3,0,0,6,1
IO,4,Q0,0,9,0,0,8,9
IO,5,Q0,0,9,0,0,8,9
MIX,4,Q2,3,9,0,0,8,16
MIX,5,Q0,0,6,0,0,6,16
COMP_CPU1,4,Q2,5,5,0,0,5,1
COMP_CPU2,5,Q2,4,3,0,0,5,1
COMP_IO1,6,Q0,0,9,1,1,9,9
COMP_MIX1,7,Q1,4,10,1,1,10,11
```

---

### 5. Quantitative MLFQ Evaluation & Analysis

#### 5.1 Priority Demotion Dynamics ($Q_0 \to Q_1 \to Q_2$)
- In `mlfqbench_cpu`, both worker processes started in $Q_0$.
- After consuming their initial 1-tick quantum in $Q_0$, they were preempted by the timer interrupt and demoted to $Q_1$.
- Upon exhausting their 2-tick quantum in $Q_1$, they were demoted to $Q_2$ (quantum = 4 ticks).
- Both finished with priority `Q2`, confirming that compute-bound tasks are systematically moved out of the high-priority queues.

#### 5.2 I/O Responsiveness & Priority Retention
- In `mlfqbench_io`, workers performed short compute bursts followed by voluntary sleep (`pause(1)`).
- Because they voluntarily yielded before exhausting their 1-tick quantum in $Q_0$, their priority was maintained at `Q0` throughout their entire lifecycle.
- Each I/O worker was scheduled 9 times with low response time (0–1 ticks), validating latency isolation for interactive tasks.

#### 5.3 Contention and Multiplexing
- In `mlfqbench_comp`, CPU-bound tasks, I/O tasks, and mixed tasks competed simultaneously:
  - CPU-bound tasks dropped to $Q_2$.
  - I/O tasks remained in $Q_0$.
  - Mixed tasks balanced in $Q_1$ with 10 dispatches and 11 system calls.
- Even under heavy CPU contention, the I/O-bound task maintained a low response time of 1 tick and waiting time of 1 tick, proving that MLFQ prevents CPU hogs from starving interactive applications.

---

### 6. Interactive CLI Tool: `kstats`

The `kstats` user utility provides live process inspection:
```
$ kstats
PID    PPID   STATE        PRIO   TICKS  SCHED  RESP   WAIT   TURN   SCALL  NAME
1      0      SLEEPING     Q0     0      24     0      0      120    0      init
2      1      SLEEPING     Q0     0      13     0      0      119    5      sh
3      2      RUNNING      Q0     0      8      0      0      0      1      kstats
```

---

### 7. Verification & Regression Results

1. **Unit & Sanity Tests**:
   - `ps`: PASSED (PID, PPID, STATE, PRIO, SIZE, TICKS, SCHED, NAME displayed accurately).
   - `kstatstest`: PASSED (all metrics verified; `syscall_count`, `response_time`, `wait_ticks`, `turnaround_time`).
   - `tracetest`: PASSED (all 5 system call tracing tests passed).
   - `mlfqtest`: PASSED (all MLFQ scheduling, demotion, and retention tests passed).
2. **xv6 Regression Suite**:
   - `python3 test-xv6.py -q usertests`
   - Result: **ALL TESTS PASSED** (zero regressions across copyin, copyout, fork, exec, memory, sbrk, filesystem, lazy allocation, and pipes).

---

### 8. Academic Viva Defense & Examination Q&A

**Q1: Why is turnaround time different from CPU time?**
> *Turnaround time* is the total wall-clock time elapsed from process creation to termination ($T_{\text{exit}} - T_{\text{ctime}}$), encompassing CPU execution, ready waiting time, and sleep/blocking time. *CPU time* (`cpu_ticks`) exclusively measures the cycles during which the process was actively running on the CPU.

**Q2: How does MLFQ prevent priority inversion and starvation?**
> MLFQ employs a periodic priority boost (`mlfq_boost()`) every 100 ticks that resets all active processes to $Q_0$. This guarantees that low-priority CPU-bound processes in $Q_2$ are not permanently starved by newly arriving high-priority tasks.

**Q3: How was race condition avoided between process termination and `getpinfo()`?**
> When a process terminates, it transitions to `ZOMBIE` state in `kexit()` and records `etime = ticks`. It remains in the process table until reaped by `wait()`. `proc_getpinfo()` inspects zombie processes safely by acquiring `p->lock` and computes $T_{\text{turn}} = p\to\text{etime} - p\to\text{ctime}$. Reaping is only performed after benchmarks record metrics.

**Q4: Why read `ticks` without acquiring `tickslock` in `proc.c`?**
> In xv6, timer interrupts acquire `tickslock` and subsequently acquire `p->lock` (e.g., during `wakeup`). Acquiring `tickslock` inside `scheduler()` or `proc.c` while already holding `p->lock` would cause a lock-order inversion deadlock. Since `ticks` is a 32-bit integer, reading it is an atomic single-instruction load on RISC-V 64-bit architectures.
