# System Call Tracing (Feature 3) Test Report

**Project Title**: Enhanced Operating System Kernel Based on xv6  
**Repository**: MIT xv6-riscv  
**Date**: September 15, 2026  
**Status**: All Tests Passed Successfully  

---

## 1. Feature Objective

The objective of Feature 3 is to implement a centralized, per-process **System Call Tracing** facility in the xv6 operating system kernel. When tracing is enabled, the kernel monitors system calls invoked by the process (and its forked children), printing informative trace records whenever a traced system call executes.

---

## 2. Design Overview

The design follows a centralized interception architecture:
1. **Per-Process Trace State**: Each process maintains a `trace_mask` integer in `struct proc`.
2. **Centralized Syscall Interception**: In `syscall()` (`kernel/syscall.c`), immediately after the system call handler returns its result, the kernel checks whether the corresponding bit in `p->trace_mask` is set.
3. **Formatted Logging**: If enabled, the kernel emits a structured trace record using `printk()`:
   ```text
   trace: pid=<PID> name=<NAME> syscall=<SYSCALL_NAME> num=<SYSCALL_NUM> return=<RETURN_VALUE>
   ```
4. **Child Inheritance Across `fork()`**: In `kfork()`, the child inherits `p->trace_mask` from its parent, allowing commands executed via `exec()` under the `trace` utility to be monitored seamlessly.
5. **No Overhead for Untraced Syscalls**: An untraced process has `trace_mask == 0`, adding only a single bitwise test `(0 & (1 << num))` per syscall.

---

## 3. Files Modified

| File | Changes Made |
|---|---|
| [`kernel/syscall.h`](file:///home/karthikeyan/OS_Project/xv6-riscv/kernel/syscall.h) | Defined system call number `#define SYS_trace 24`. |
| [`kernel/syscall.c`](file:///home/karthikeyan/OS_Project/xv6-riscv/kernel/syscall.c) | Declared `sys_trace`, added to `syscalls[]` dispatch table, defined `syscall_names[]` mapping, and integrated centralized tracing output. |
| [`kernel/sysproc.c`](file:///home/karthikeyan/OS_Project/xv6-riscv/kernel/sysproc.c) | Implemented `sys_trace(void)` system call handler. |
| [`kernel/proc.h`](file:///home/karthikeyan/OS_Project/xv6-riscv/kernel/proc.h) | Added `int trace_mask;` to `struct proc`. |
| [`kernel/proc.c`](file:///home/karthikeyan/OS_Project/xv6-riscv/kernel/proc.c) | Initialized `p->trace_mask = 0;` in `allocproc()`; propagated `np->trace_mask = p->trace_mask;` in `kfork()`. |
| [`user/user.h`](file:///home/karthikeyan/OS_Project/xv6-riscv/user/user.h) | Added user-space prototype `int trace(int);`. |
| [`user/usys.pl`](file:///home/karthikeyan/OS_Project/xv6-riscv/user/usys.pl) | Added `entry("trace");` to generate assembly stub. |
| [`Makefile`](file:///home/karthikeyan/OS_Project/xv6-riscv/Makefile) | Registered `$U/_trace` and `$U/_tracetest` in `UPROGS`. |

---

## 4. Syscall Interface

```c
int trace(int mask);
```
- **Arguments**: `mask` (32-bit integer bitmask).
- **Return Value**: `0` on success, `-1` on error.
- **Behavior**: Sets the calling process's `trace_mask` to `mask`. Passing `mask = 0` disables tracing.

---

## 5. Trace Mask Design

The mask uses standard bit shifting where bit position $k$ matches `SYS_<name>`:
$$\text{bit} = (1 \ll \text{syscall\_number})$$

| Syscall Number | Macro | Mask (Binary / Decimal) | Example Tracing Command |
|---|---|---|---|
| 1 | `SYS_fork` | `1 << 1` = `2` | `trace 2 <cmd>` |
| 5 | `SYS_read` | `1 << 5` = `32` | `trace 32 <cmd>` |
| 7 | `SYS_exec` | `1 << 7` = `128` | `trace 128 <cmd>` |
| 13 | `SYS_pause` | `1 << 13` = `8192` | `trace 8192 <cmd>` |
| 15 | `SYS_open` | `1 << 15` = `32768` | `trace 32768 <cmd>` |
| 16 | `SYS_write` | `1 << 16` = `65536` | `trace 65536 <cmd>` |
| 21 | `SYS_close` | `1 << 21` = `2097152` | `trace 2097152 <cmd>` |
| 24 | `SYS_trace` | `1 << 24` = `16777216` | `trace 16777216 <cmd>` |
| All | All defined | `0x7FFFFFFF` = `2147483647` | `trace 2147483647 <cmd>` |

Multiple system calls are traced by combining bits with bitwise OR (e.g., `(1 << SYS_open) | (1 << SYS_close) = 32768 + 2097152 = 2129920`).

---

## 6. Syscall Name Mapping

Defined in `kernel/syscall.c` covering all 24 defined system calls:
```c
static const char *syscall_names[] = {
  [SYS_fork]    = "fork",
  [SYS_exit]    = "exit",
  [SYS_wait]    = "wait",
  [SYS_pipe]    = "pipe",
  [SYS_read]    = "read",
  [SYS_kill]    = "kill",
  [SYS_exec]    = "exec",
  [SYS_fstat]   = "fstat",
  [SYS_chdir]   = "chdir",
  [SYS_dup]     = "dup",
  [SYS_getpid]  = "getpid",
  [SYS_sbrk]    = "sbrk",
  [SYS_pause]   = "pause",
  [SYS_uptime]  = "uptime",
  [SYS_open]    = "open",
  [SYS_write]   = "write",
  [SYS_mknod]   = "mknod",
  [SYS_unlink]  = "unlink",
  [SYS_link]    = "link",
  [SYS_mkdir]   = "mkdir",
  [SYS_close]   = "close",
  [SYS_sync]    = "sync",
  [SYS_getpinfo]= "getpinfo",
  [SYS_trace]   = "trace",
};
```

---

## 7. Process Trace State

Stored in `struct proc` (`kernel/proc.h`):
```c
struct proc {
  ...
  int trace_mask; // System call tracing bitmask
};
```
- In `allocproc()`: `p->trace_mask = 0;` ensures clean initialization.
- In `sys_trace()`: `myproc()->trace_mask = mask;` dynamically updates the mask.

---

## 8. Fork Behavior & Inheritance

In `kernel/proc.c:kfork()`:
```c
safestrcpy(np->name, p->name, sizeof(p->name));
np->trace_mask = p->trace_mask;
```
When `trace.c` executes:
1. It calls `trace(mask)` to set its own `trace_mask`.
2. It calls `exec(command, args)` (or child forked by shell).
3. The process retains `trace_mask` across `exec()`.
4. Any future `fork()` calls by that command inherit `trace_mask`, ensuring entire process subtrees are accurately traced.

---

## 9. Test Programs Created

1. **[`user/tracetest.c`](file:///home/karthikeyan/OS_Project/xv6-riscv/user/tracetest.c)**:
   - Automated unit test suite verifying:
     - Single syscall tracing (`SYS_pause`).
     - Multi-syscall tracing (`SYS_open` and `SYS_close`).
     - Error return value reporting (negative return codes).
     - Fork inheritance across child processes (`SYS_getpid`).
     - Disabling tracing (`mask = 0`).
2. **[`user/trace.c`](file:///home/karthikeyan/OS_Project/xv6-riscv/user/trace.c)**:
   - User-space CLI tool allowing syntax:
     ```text
     $ trace <mask> <command> [args...]
     ```

---

## 10. Test Results

| Test | Purpose | Expected Result | Actual Result | Status |
|---|---|---|---|---|
| **tracetest (Test 1)** | Single syscall mask | Only `pause` produces trace; `getpid` silent | `trace: pid=3 name=tracetest syscall=pause num=13 return=0` | **PASSED** |
| **tracetest (Test 2)** | Multi-syscall mask | Both `open` and `close` traced | `trace: pid=3 name=tracetest syscall=open num=15 return=3`<br>`trace: pid=3 name=tracetest syscall=close num=21 return=0` | **PASSED** |
| **tracetest (Test 3)** | Error return value | Negative return reported | `trace: pid=3 name=tracetest syscall=open num=15 return=-1` | **PASSED** |
| **tracetest (Test 4)** | Fork inheritance | Child inherits mask and traces `getpid` | `trace: pid=4 name=tracetest syscall=getpid num=11 return=4` | **PASSED** |
| **tracetest (Test 5)** | Disable tracing | `trace(0)` silences all output | Zero trace messages generated | **PASSED** |
| **trace 32 ls** | CLI tool tracing `read` | Only `read` syscalls printed during `ls` | `trace: pid=3 name=ls syscall=read num=5 return=16` ... `return=0` | **PASSED** |
| **trace 2147483647 echo hello** | Tracing all syscalls | Traces `trace`, `exec`, `write` | `trace: pid=4 name=trace syscall=trace num=24 return=0`<br>`trace: pid=4 name=echo syscall=exec num=7 return=2`<br>`trace: pid=4 name=echo syscall=write num=16 return=5` | **PASSED** |
| **trace 32768 cat README** | CLI tool tracing `open` | Traces `open` syscall | `trace: pid=5 name=cat syscall=open num=15 return=3` | **PASSED** |
| **ps** | Feature 1 sanity | Displays process status table with PRIO | Unaffected; processes shown cleanly | **PASSED** |
| **schedtest** | Feature 2 sanity | Validates MLFQ scheduling stats | All 5 MLFQ checklist items PASSED | **PASSED** |
| **usertests** | Full OS regression | 45+ core kernel tests pass | `ALL TESTS PASSED` | **PASSED** |

---

## 11. Trace Output Examples

### Running `tracetest`:
```text
$ tracetest
=== System Call Tracing Validation Suite ===

[Test 1] Testing single syscall tracing (SYS_pause)...
Expected output below: trace message for 'pause', none for 'getpid'
trace: pid=3 name=tracetest syscall=pause num=13 return=0
[Test 1] PASSED.

[Test 2] Testing multi-syscall tracing (open & close)...
Expected output below: trace messages for 'open' and 'close'
trace: pid=3 name=tracetest syscall=open num=15 return=3
trace: pid=3 name=tracetest syscall=close num=21 return=0
[Test 2] PASSED.

[Test 3] Testing error return value reporting (open non-existent)...
Expected output below: trace message for 'open' with return=-1
trace: pid=3 name=tracetest syscall=open num=15 return=-1
[Test 3] PASSED.

[Test 4] Testing trace mask inheritance across fork()...
Expected output below: child process traces 'getpid'
trace: pid=4 name=tracetest syscall=getpid num=11 return=4
[Test 4] PASSED.

[Test 5] Testing trace disable (mask = 0)...
Expected output below: NO trace messages should appear
[Test 5] PASSED.

=== ALL TRACE TESTS PASSED ===
```

### Running `trace 2147483647 echo hello`:
```text
$ trace 2147483647 echo hello
trace: pid=4 name=trace syscall=trace num=24 return=0
trace: pid=4 name=echo syscall=exec num=7 return=2
hellotrace: pid=4 name=echo syscall=write num=16 return=5

trace: pid=4 name=echo syscall=write num=16 return=1
```

### Running `trace 32768 cat README`:
```text
$ trace 32768 cat README
trace: pid=5 name=cat syscall=open num=15 return=3
xv6 is a re-implementation of Dennis Ritchie's and Ken Thompson's Unix...
```

---

## 12. Regression Test Results

Running `python3 test-xv6.py -q usertests`:
```text
Namespace(testrex='usertests', q=True)
...
test copyin: OK
test copyout: OK
test preempt: kill... wait... OK
test exitwait: OK
test sbrkbasic: OK
test lazy_alloc: OK
test unlinkcwd: OK
ALL TESTS PASSED
```

---

## 13. Bugs Discovered & Fixes Made

- **Clean Build Target Dependency**: When running `make clean && make fs.img`, `kernel/kernel` was removed by `clean` and needed to be re-linked with `make`. Running full `make && make fs.img` properly generated all targets.
- **Kernel Tracing Accuracy**: No kernel bugs, deadlocks, or races occurred. The centralized dispatch hook reliably captured return values for both successful and failing syscalls.

---

## 14. Limitations

1. **32-Bit Mask Capacity**: The mask is an integer supporting up to 31 system call bits. Since xv6 currently defines 24 system calls, this fits within standard 32-bit registers.
2. **Argument Values**: In accordance with the project specification, tracing logs system call numbers, names, and return values. Argument values (such as string pointers or file paths) are not logged to avoid buffer-decoding overhead in the central dispatch path.

---

## 15. Viva Demonstration Guide

To demonstrate Feature 3 to your examiner:
1. **Show automated validation**:
   Run `tracetest`. Point out how only the requested syscalls are printed, how return values match, and how the child process inherits the trace mask across `fork()`.
2. **Show live command tracing**:
   Run `trace 32 ls`. Explain: *"32 corresponds to `1 << SYS_read` (bit 5). The kernel traces each directory block read as `ls` scans the disk."*
3. **Show error return value tracing**:
   Run `tracetest` or `trace 32768 cat nonexistent`. Explain: *"When `cat` attempts to open a missing file, the trace log clearly displays `return=-1`, confirming return-value capture."*
4. **Show code centralization**:
   Open `kernel/syscall.c`. Show that rather than modifying 24 individual syscall functions, all tracing is handled in just 5 lines inside `syscall()`.
