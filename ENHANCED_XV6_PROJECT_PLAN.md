# Project Architecture & Implementation Plan: Enhanced Operating System Kernel Based on xv6

**Project Title**: Enhanced Operating System Kernel Based on xv6  
**Base Operating System**: MIT xv6-riscv (RISC-V 64-bit Sv39)  
**Document Status**: Architectural Analysis & Implementation Blueprint (Pre-Implementation Review)

---

## Executive Summary & Codebase Overview

xv6 is a re-implementation of Dennis Ritchie and Ken Thompson's Unix Version 6 (v6) for a modern multi-core RISC-V microprocessor. The kernel is written in C99/GNU99 with small RISC-V assembly routines, running in supervisor mode (`S-mode`), while user programs run in user mode (`U-mode`), and hardware initialization boots in machine mode (`M-mode`).

### Hardware Target Environment
- **Architecture**: RISC-V 64-bit (RV64GC: `rv64imafdc`)
- **Memory Management Unit**: Sv39 paging (3-level page tables, 39-bit virtual address space, 4 KB pages)
- **Timer Extension**: RISC-V `sstc` extension (`stimecmp` register in S-mode)
- **Emulated Hardware (QEMU `virt` machine)**:
  - RAM: 128 MB mapped from physical address `0x80000000` (`KERNBASE`) to `0x88000000` (`PHYSTOP`)
  - UART: `0x10000000` (16550-compatible serial port)
  - VirtIO Block Device: `0x10001000` (disk controller)
  - PLIC (Platform-Level Interrupt Controller): `0x0C000000`
  - CLINT (Core Local Interruptor): `0x02000000`

### Core Development Principles
1. **Zero Degradation**: Retain full backward compatibility; baseline tests (`usertests`, `test-xv6.py`) must pass.
2. **Modular Progression**: Feature → Build → Test → Explain → Commit → Next Feature.
3. **No Unrelated Modifications**: Isolate changes strictly to subsystem boundaries.

---

## Part 1: Detailed Codebase Subsystem Analysis

Below is the analysis of the 11 key architectural subsystems in the current repository, identifying their primary source files, data structures, and core functions.

```
+-------------------------------------------------------------------------------+
|                                  USER SPACE                                   |
|   User Programs: sh, init, cat, ls, usertests, etc.                           |
|   User Library: ulib.c, printf.c, umalloc.c | Syscall Stubs: usys.S           |
+-------------------------------------------------------------------------------+
                                  | ecall / sret
+-------------------------------------------------------------------------------+
|                                 KERNEL SPACE                                  |
|  [Traps & Syscalls]   trap.c, trampoline.S, syscall.c, sysproc.c, sysfile.c   |
|  [Process Subsystem]  proc.c, swtch.S (allocproc, kfork, scheduler, sched)    |
|  [Virtual Memory]     vm.c (walk, mappages, uvmcopy, vmfault, copyin/copyout) |
|  [Physical Memory]    kalloc.c (kinit, kalloc, kfree, freelist)               |
|  [Filesystem Layers]  fs.c, bio.c, log.c, file.c, virtio_disk.c               |
+-------------------------------------------------------------------------------+
                                  | MMIO / CSR / Interrupts
+-------------------------------------------------------------------------------+
|                         RISC-V HARDWARE / QEMU VIRT                           |
+-------------------------------------------------------------------------------+
```

### 1. Process Management
- **Key Files**: `kernel/proc.c`, `kernel/proc.h`, `kernel/defs.h`, `kernel/param.h`
- **Key Functions**:
  - `procinit()`: Initializes spinlocks for PID allocator and process table entries.
  - `allocpid()`: Safely increments `nextpid` under `pid_lock`.
  - `allocproc()`: Allocates an `UNUSED` process slot, sets up `trapframe`, creates user page table (`proc_pagetable`), sets context `ra` to `forkret`, and returns with `p->lock` held.
  - `freeproc()`: Frees `trapframe`, frees user page table (`proc_freepagetable`), resets fields, and sets state to `UNUSED`.
  - `userinit()`: Sets up the initial process (`initproc`) with `cwd = namei("/")` and marks it `RUNNABLE`.
  - `kfork()`: Duplicates caller's virtual memory via `uvmcopy`, copies trapframe registers (setting child's `a0 = 0`), duplicates open file descriptors (`filedup`), links `p->parent`, and sets child state to `RUNNABLE`.
  - `kexit()`: Closes open files, reparents abandoned children to `initproc`, wakes sleeping parent, sets state to `ZOMBIE`, and enters `sched()`.
  - `kwait()`: Scans process table for `ZOMBIE` children, copies out exit status, cleans child with `freeproc()`, or sleeps on `wait_lock` if children are still active.
  - `growproc()`: Grows or shrinks user virtual memory size (`p->sz`) using `uvmalloc` or `uvmdealloc`.
  - `sleep_prepare()` / `sleep()` / `wakeup()`: Condition synchronization mechanism using channel pointers (`p->chan`).

### 2. Process Structure
- **Key Files**: `kernel/proc.h`
- **Data Structures**:
  - `struct proc`:
    ```c
    struct proc {
      struct spinlock lock;        // Protects state, chan, killed, xstate, pid
      enum procstate state;        // UNUSED, USED, SLEEPING, RUNNABLE, RUNNING, ZOMBIE
      void *chan;                  // Sleep channel
      int killed;                  // Kill flag
      int xstate;                  // Exit status
      int pid;                     // Process ID
      struct proc *parent;         // Parent process pointer
      uint64 kstack;               // Kernel stack VA
      uint64 sz;                   // Size of user memory in bytes
      pagetable_t pagetable;       // User page table root
      struct trapframe *trapframe; // Page holding saved user registers
      struct context context;      // Callee-saved registers for swtch
      struct file *ofile[NOFILE];  // Table of open files
      struct inode *cwd;           // Current working directory
      char name[16];               // Process name
    };
    ```
  - `struct trapframe`: Maps 32 user registers (`ra`, `sp`, `gp`, `tp`, `t0-t6`, `s0-s11`, `a0-a7`), saved user `epc`, and kernel switch vectors (`kernel_satp`, `kernel_sp`, `kernel_trap`, `kernel_hartid`).
  - `struct context`: Callee-saved registers (`ra`, `sp`, `s0-s11`) saved across kernel context switches.
  - `struct cpu`: Tracks CPU state (`proc`, `context`, `noff`, `intena`).
- **Lock Ordering Rules**:
  - Always acquire `wait_lock` before any `p->lock`.
  - To prevent deadlock when locking two processes, always lock the process with lower address first (or acquire child lock before parent when specified).

### 3. Scheduler
- **Key Files**: `kernel/proc.c`, `kernel/swtch.S`
- **Key Functions**:
  - `scheduler()`: The per-CPU scheduler loop. Runs on CPU scheduler stack. Loops across the `proc[]` array in round-robin fashion, finds a `RUNNABLE` process, locks it, marks it `RUNNING`, sets `c->proc = p`, and calls `swtch(&c->context, &p->context)`. If no process is runnable, it executes `wfi` (Wait For Interrupt).
  - `sched()`: Called by a process yielding, sleeping, or exiting. Validates preconditions (holding only `p->lock`, `p->state != RUNNING`, interrupts disabled), then switches back to `c->context`.
  - `yield()`: Acquires `p->lock`, sets `p->state = RUNNABLE`, calls `sched()`, and releases `p->lock`.
  - `swtch(struct context *old, struct context *new)`: Assembly function saving callee-saved registers into `old` and loading `new`.

### 4. System Calls
- **Key Files**: `kernel/syscall.h`, `kernel/syscall.c`, `kernel/sysproc.c`, `kernel/sysfile.c`
- **Key Functions**:
  - `syscall()`: Invoked from `usertrap()`. Reads syscall number from `p->trapframe->a7`, indexes into `syscalls[]` function pointer table, calls the implementation, and stores return value in `p->trapframe->a0`.
  - `argraw(int n)`: Retrieves the raw n-th argument from `p->trapframe->a0` through `a5`.
  - `argint(int n, int *ip)`: Retrieves an integer argument.
  - `argaddr(int n, uint64 *ip)`: Retrieves a pointer/address argument.
  - `argstr(int n, char *buf, int max)`: Retrieves a null-terminated string argument from user space using `copyinstr`.
  - `fetchaddr()` / `fetchstr()`: Reads data from user space virtual address into kernel buffer.

### 5. Virtual Memory
- **Key Files**: `kernel/vm.c`, `kernel/memlayout.h`, `kernel/riscv.h`
- **Memory Layout**:
  - **Kernel Space**: Direct-mapped for physical devices (UART `0x10000000`, VirtIO `0x10001000`, PLIC `0x0C000000`), kernel code (`KERNBASE` to `etext`, `PTE_R|PTE_X`), kernel data & RAM (`etext` to `PHYSTOP`, `PTE_R|PTE_W`). Highest addresses map `TRAMPOLINE` and process kernel stacks `KSTACK(p)` with guard pages.
  - **User Space**: Starts at virtual address `0x0` (`text`, `data`, `bss`), followed by user stack and expanding heap. Just below the top (`MAXVA`), `TRAPFRAME` is mapped at `MAXVA - 2*PGSIZE`, and `TRAMPOLINE` is mapped at `MAXVA - PGSIZE`.
- **Key Functions**:
  - `kvmmake()` / `kvminit()` / `kvminithart()`: Builds kernel page table and programs `satp`.
  - `uvmcreate()`: Allocates an empty root page table.
  - `uvmalloc()` / `uvmdealloc()`: Expands and shrinks user virtual memory.
  - `uvmcopy()`: Clones page tables and allocates physical pages for child process.
  - `uvmfree()`: Tears down user mappings and page tables.
  - `copyin()` / `copyout()` / `copyinstr()`: Safely transfers data between user virtual addresses and kernel pointers.

### 6. Page Tables
- **Key Files**: `kernel/vm.c`, `kernel/riscv.h`
- **Sv39 Structure**:
  - 3-level tree: L2 (root, 9 bits), L1 (intermediate, 9 bits), L0 (leaf, 9 bits), Offset (12 bits). Total = 39 bits.
  - Each page table node contains 512 entries of 64 bits (`pte_t`).
  - Bit layout: `[63:54 reserved] [53:10 PPN] [9:8 RSW] [7 D] [6 A] [5 G] [4 U] [3 X] [2 W] [1 R] [0 V]`.
- **Key Functions**:
  - `walk(pagetable, va, alloc)`: Walks the 3-level Sv39 radix tree for virtual address `va`. Allocates intermediate page tables if `alloc != 0`. Returns pointer to leaf PTE.
  - `walkaddr(pagetable, va)`: Translates user virtual address to physical address, checking valid and user permissions.
  - `mappages(pagetable, va, size, pa, perm)`: Installs PTE mappings in leaf nodes for a range of pages.
  - `uvmunmap(pagetable, va, npages, do_free)`: Clears leaf PTEs and optionally calls `kfree()` on physical pages.
  - `freewalk(pagetable)`: Recursively frees intermediate page-table pages.

### 7. Traps and Page Faults
- **Key Files**: `kernel/trampoline.S`, `kernel/trap.c`, `kernel/kernelvec.S`, `kernel/start.c`
- **Key Traps**:
  - `scause == 8`: User `ecall` (System Call).
  - `scause == 13`: Load Page Fault.
  - `scause == 15`: Store/AMO Page Fault.
  - `scause == 0x8000000000000005L`: Timer Interrupt (supervisor software interrupt / sstc timer).
  - `scause == 0x8000000000000009L`: External Device Interrupt (via PLIC).
- **Key Functions**:
  - `uservec`: Trap landing pad in `trampoline.S`. Swaps `a0` with `sscratch`, stores all user registers in `TRAPFRAME`, loads kernel stack pointer `sp` and kernel page table `satp`, and jumps to `usertrap()`.
  - `usertrap()`: C trap dispatcher in S-mode. Directs system calls to `syscall()`, device interrupts to `devintr()`, page faults to `vmfault()`, and yields CPU on timer tick.
  - `prepare_return()` / `userret`: Sets `stvec` back to `uservec`, configures `sstatus` (SPP=0, SPIE=1) and `sepc = p->trapframe->epc`, switches `satp` to user page table, restores user registers from `TRAPFRAME`, and executes `sret`.
  - `kernelvec` / `kerneltrap()`: Handles traps occurring while executing inside kernel mode.
  - `vmfault()`: Existing lazy allocation handler that allocates and maps a physical page on page faults caused by lazy `sys_sbrk`.

### 8. Physical Memory Allocation
- **Key Files**: `kernel/kalloc.c`
- **Data Structures**:
  - `struct run`: Singly-linked list node embedded directly in free 4096-byte pages.
  - `struct { struct spinlock lock; struct run *freelist; } kmem`: Free-list protected by spinlock.
- **Key Functions**:
  - `kinit()`: Initializes `kmem.lock` and calls `freerange(end, PHYSTOP)`.
  - `freerange(pa_start, pa_end)`: Calls `kfree()` for every page between kernel `end` and `PHYSTOP`.
  - `kalloc()`: Pops a 4096-byte page from `freelist`, fills it with garbage byte `0x05`, and returns physical pointer.
  - `kfree(pa)`: Validates alignment and bounds, fills page with garbage byte `0x01`, pushes it onto `freelist`.

### 9. Filesystem
- **Key Files**: `kernel/fs.h`, `kernel/fs.c`, `kernel/bio.c`, `kernel/log.c`, `kernel/file.h`, `kernel/file.c`, `kernel/sysfile.c`, `kernel/virtio_disk.c`
- **Disk Layout** (`BSIZE = 1024`):
  `[Block 0: Boot] [Block 1: Superblock] [Blocks 2..31: Log] [Blocks 32..44: Inodes] [Block 45: Bitmap] [Blocks 46..1999: Data]`
- **Architectural Layers**:
  1. **Disk**: `virtio_disk.c` (VirtIO MMIO queues, DMA transfers, interrupt handler `virtio_disk_intr`).
  2. **Buffer Cache**: `bio.c` (`bread`, `bwrite`, `brelse`, `bpin`, `bunpin`). Fixed pool of `NBUF = 30` buffers organized in doubly-linked circular list with sleep-locks.
  3. **Logging**: `log.c` (`begin_op`, `end_op`, `log_write`). Write-ahead logging for crash consistency and atomic transactions.
  4. **Inodes**: `fs.c` (`ialloc`, `iget`, `iput`, `ilock`, `iunlock`, `iupdate`). In-memory `struct inode` cache protected by `itable.lock` and per-inode `sleeplock`. Supports 12 direct blocks + 1 singly-indirect block (256 blocks) = 268 blocks max.
  5. **Directory**: `fs.c` (`dirlookup`, `dirlink`). Directory entries `struct dirent` (inum + 14-char name).
  6. **Path Resolution**: `fs.c` (`namei`, `nameiparent`). Iterative path traversal parsing `/` delimiters.
  7. **File Descriptors**: `file.c` (`filealloc`, `filedup`, `fileclose`, `fileread`, `filewrite`, `filestat`). Global file table `ftable` mapping pipes, inodes, and device major numbers.

### 10. User-Space Programs
- **Key Files**: `user/user.h`, `user/usys.pl`, `user/ulib.c`, `user/printf.c`, `user/umalloc.c`, `user/init.c`, `user/sh.c`, `user/user.ld`
- **Runtime Mechanism**:
  - `user.ld`: Links user binaries starting at virtual address `0x0`.
  - `start()` in `ulib.c`: Standard entry point called by the kernel loader (`kexec`). Calls `main(argc, argv)` and passes return value to `exit()`.
  - `usys.pl`: Perl script generating `usys.S` syscall stubs (`li a7, SYS_xxx; ecall; ret`).
  - `umalloc.c`: User-space memory allocator using first-fit algorithm over `sbrk()`.

### 11. Build System
- **Key Files**: `Makefile`, `kernel/kernel.ld`, `user/user.ld`, `mkfs/mkfs.c`, `test-xv6.py`
- **Build Flow**:
  1. Auto-detects RISC-V cross-compiler prefix (`TOOLPREFIX`).
  2. Compiles kernel objects into `$K/kernel` using linker script `kernel.ld`.
  3. Uses `usys.pl` to produce `usys.S`, compiles user C files with `user.ld`, and generates individual binaries `user/_<name>`.
  4. Compiles host tool `mkfs/mkfs`, packages user binaries and `README` into raw disk image `fs.img`.
  5. Invokes QEMU: `qemu-system-riscv64 -machine virt -bios none -kernel kernel/kernel -m 128M -smp 3 -nographic -drive file=fs.img,if=none,format=raw,id=x0 -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0`.

---

## Part 2: Operating System Core Mechanisms Explained

```
+---------------------------------------------------------------------------------------------------+
| USER SPACE                                                                                        |
| 1. user program calls read() in user/user.h                                                       |
| 2. read stub in user/usys.S puts SYS_read in a7, runs "ecall"                                      |
+---------------------------------------------------------------------------------------------------+
                                            | Hardware Trap (ecall)
                                            v
+---------------------------------------------------------------------------------------------------+
| TRAMPOLINE (uservec)                                                                              |
| 3. Swap a0 with sscratch; a0 = TRAPFRAME                                                          |
| 4. Save 32 user registers in TRAPFRAME                                                            |
| 5. Load kernel_sp, kernel_hartid, kernel_satp, kernel_trap (usertrap)                             |
| 6. sfence.vma; csrw satp, kernel_satp; sfence.vma; jalr usertrap                                 |
+---------------------------------------------------------------------------------------------------+
                                            | Jump to C Handler
                                            v
+---------------------------------------------------------------------------------------------------+
| KERNEL SPACE (usertrap & syscall)                                                                 |
| 7. w_stvec(kernelvec) -> prepare for kernel-mode traps                                            |
| 8. p->trapframe->epc = r_sepc(); p->trapframe->epc += 4 (advance past ecall)                       |
| 9. intr_on(); syscall();                                                                          |
| 10. syscall() inspects p->trapframe->a7 -> invokes sys_read()                                     |
| 11. Return value placed in p->trapframe->a0                                                       |
+---------------------------------------------------------------------------------------------------+
                                            | Return to User Mode
                                            v
+---------------------------------------------------------------------------------------------------+
| TRAMPOLINE (userret)                                                                              |
| 12. prepare_return(): w_stvec(trampoline_uservec); set sstatus SPP=0, SPIE=1; w_sepc(epc)        |
| 13. userret in trampoline.S switches satp back to user_satp                                       |
| 14. Restores user registers from TRAPFRAME (a0 contains return value)                             |
| 15. sret -> Returns to user mode at saved epc                                                     |
+---------------------------------------------------------------------------------------------------+
```

### 1. How a User Program Calls the Kernel
1. User application calls a library function declared in `user.h` (e.g., `write(1, "hello\n", 6)`).
2. The linker resolves this to the stub in `usys.S`:
   ```assembly
   write:
     li a7, SYS_write    # Load system call number 16 into register a7
     ecall               # Environment Call: hardware raises trap to supervisor mode
     ret
   ```
3. The hardware switches privilege level to Supervisor (`S-mode`), disables interrupts, saves current PC to `sepc`, records cause 8 in `scause`, and jumps to the address in `stvec`.
4. Prior to returning to user space, the kernel set `stvec` to point to `uservec` in the `trampoline` page.

### 2. How System Calls Work
1. `uservec` in `trampoline.S` executes in S-mode but still using the **user page table**:
   - Swaps user `a0` with `sscratch`. `a0` now points to `TRAPFRAME` (`MAXVA - 2*PGSIZE`).
   - Saves all general registers (`ra`, `sp`, `gp`, `tp`, `t0-t6`, `s0-s11`, `a1-a7`, and original `a0` from `sscratch`) into `p->trapframe`.
   - Restores kernel stack `sp`, kernel thread pointer `tp` (hartid), and loads `kernel_satp`.
   - Switches `satp` to kernel page table and flushes TLB via `sfence.vma`.
   - Jumps to `usertrap()` in `kernel/trap.c`.
2. In `usertrap()`:
   - Points `stvec` to `kernelvec` so that any trap inside the kernel is handled by `kerneltrap()`.
   - Saves `sepc` into `p->trapframe->epc` and increments it by 4 (`epc += 4`) so return resumes after `ecall`.
   - Checks `r_scause() == 8`, re-enables interrupts via `intr_on()`, and calls `syscall()`.
3. In `syscall()`:
   - Reads `num = p->trapframe->a7`.
   - Calls `syscalls[num]()`. Syscall arguments are unpacked using `argint`, `argaddr`, `argstr` (reading `p->trapframe->a0..a5`).
   - Stores return value into `p->trapframe->a0`.
4. Execution returns through `prepare_return()` and `userret` in `trampoline.S`:
   - Sets `stvec` back to `trampoline_uservec`.
   - Programs `sstatus` with `SPP = 0` (return to U-mode) and `SPIE = 1`.
   - Sets `sepc = p->trapframe->epc`.
   - Switches `satp` to user page table, restores all user registers from `TRAPFRAME`, restores `a0` (carrying syscall return value), and calls `sret`.

### 3. How Processes Are Created
1. `kfork()` is invoked (via `sys_fork()`):
   - Calls `allocproc()`: Finds an `UNUSED` process in `proc[NPROC]`, allocates PID, allocates `trapframe` page via `kalloc()`, and calls `proc_pagetable()` to create user page table with mappings for `TRAMPOLINE` and `TRAPFRAME`. Sets kernel context `ra = (uint64)forkret` and `sp = p->kstack + PGSIZE`.
   - Calls `uvmcopy()`: In standard xv6, copies all parent physical pages page-by-page and maps them into the child page table with identical flags. Sets `np->sz = p->sz`.
   - Copies user register state: `*(np->trapframe) = *(p->trapframe)`.
   - Forces return value in child to zero: `np->trapframe->a0 = 0`.
   - Increments file descriptor references (`filedup`) and duplicates CWD (`idup`).
   - Sets `np->parent = p` under `wait_lock`.
   - Transitions state to `RUNNABLE` under `np->lock`.
2. First time the child runs:
   - Scheduler switches context to `forkret`.
   - Child releases `p->lock` and jumps through `prepare_return()` and `userret`, landing in user space with `a0 = 0`.

### 4. How Processes Are Scheduled
1. Each CPU core runs `scheduler()` in `kernel/proc.c` after initialization in `main()`.
2. Loops continuously over `proc[0..NPROC-1]`:
   - Acquires `p->lock`.
   - Checks `p->state == RUNNABLE`.
   - If found:
     - Sets `p->state = RUNNING`.
     - Sets `c->proc = p`.
     - Calls `swtch(&c->context, &p->context)`.
     - CPU begins executing `p`'s kernel thread.
   - When the process yields, sleeps, or exits, it calls `sched()`, which switches back to `&c->context`.
   - Releases `p->lock`.
3. If no process is runnable, `scheduler()` executes `wfi` (Wait For Interrupt) to save power until the next device or timer interrupt.

### 5. How Context Switching Works
Context switching in xv6 is a **two-step cooperative switch** involving three threads of execution:
`Process A Kernel Thread -> Per-CPU Scheduler Thread -> Process B Kernel Thread`.

```
Process A (S-mode)                  CPU Scheduler                       Process B (S-mode)
   swtch(&p->context, &c->context) -------->
                                       finds Process B
                                       swtch(&c->context, &p->context) -------->
                                                                           resumes Process B
```
- Context switches **never** switch directly from Process A to Process B.
- `swtch(old, new)` in `swtch.S` only saves and restores the 14 callee-saved registers defined in RISC-V ABI: `ra`, `sp`, and `s0` through `s11`.
- Caller-saved registers are already preserved on the process kernel stack or user trapframe.
- When `swtch` writes the return address `ra` into `old->context.ra` and loads `new->context.ra`, the subsequent `ret` instruction immediately resumes execution in the new thread at the point where it previously called `swtch`.

### 6. How Virtual Memory Works
- xv6 uses RISC-V Sv39 paging. A 64-bit virtual address uses bits 0–38 (39 bits total):
  - `VPN[2]` (bits 30..38): Index into L2 page table (512 entries).
  - `VPN[1]` (bits 21..29): Index into L1 page table (512 entries).
  - `VPN[0]` (bits 12..20): Index into L0 page table (512 entries).
  - `Offset` (bits 0..11): Byte offset within 4 KB page.
- Address translation is handled by hardware:
  - Hardware reads root page table physical address from `satp` register.
  - Traverses the 3 levels using `walk()`.
  - In xv6, the kernel and user space maintain separate page tables:
    - `kernel_pagetable`: Active when running kernel code, direct-maps physical devices and RAM up to `PHYSTOP`.
    - `p->pagetable`: Active when in user space.
  - To allow safe transitions between page tables without crashing the PC, the `TRAMPOLINE` code page is mapped at the identical virtual address (`MAXVA - PGSIZE`) in **both** kernel and user page tables.

### 7. How Page Faults Are Handled
1. When a memory access cannot be translated or violates permissions, RISC-V hardware sets:
   - `scause`: 13 (Load Page Fault), 15 (Store/AMO Page Fault), or 12 (Instruction Page Fault).
   - `stval`: The exact virtual faulting address that triggered the exception.
   - `sepc`: The instruction that caused the fault.
2. The trap enters `usertrap()` via `uservec`.
3. In this codebase:
   ```c
   } else if ((r_scause() == 15 || r_scause() == 13) &&
              vmfault(p->pagetable, p->sz, r_stval(),
                      (r_scause() == 13) ? 1 : 0) != 0) {
     // page fault on lazily-allocated page
   }
   ```
4. `vmfault()`:
   - Checks if `va < p->sz` and not already mapped.
   - Calls `kalloc()` to allocate a new physical page.
   - Clears memory with `memset(pa, 0, PGSIZE)`.
   - Calls `mappages(pagetable, PGROUNDDOWN(va), PGSIZE, pa, PTE_W | PTE_U | PTE_R)`.
   - Returns physical address.
5. `usertrap()` exits through `prepare_return()` and `userret` without advancing `sepc` (`epc` is NOT incremented by 4), causing the CPU to re-execute the exact instruction that previously faulted, this time succeeding because the mapping exists.

### 8. How the Filesystem Works
1. **Request Flow**: User calls `read(fd, buf, n)` -> `sys_read()` in `sysfile.c` -> `fileread()` in `file.c` -> `readi()` in `fs.c`.
2. **Buffer Cache (`bio.c`)**: `bread(dev, bno)` checks if block `bno` is already cached in memory. If not, claims a buffer, calls `virtio_disk_rw()` to fetch from disk, and locks the buffer.
3. **Inodes (`fs.c`)**:
   - Disk inode (`struct dinode`) contains file metadata and block addresses `addrs[13]`.
   - `bmap(ip, bn)` maps file logical block number to disk block number (handling direct blocks 0..11 and indirect block 12).
4. **Transactions & Logging (`log.c`)**:
   - Any file system modification is bracketed by `begin_op()` and `end_op()`.
   - Inode/bitmap writes call `log_write(bp)` instead of writing directly to disk.
   - On `end_op()`, the log header is committed to disk block 2, then cached blocks are copied to their true disk locations, and log header is cleared.
   - On reboot after crash, `initlog()` inspects the log header and replays uncommitted writes, ensuring atomic updates and crash recovery.

---

## Part 3: Planned Features — Modification Blueprint

Here is the exact mapping of files and functions required for each of the 6 planned project features.

```
+---------------------------+-------------------------------------------------------------+
| Feature                   | Primary Files Involved                                      |
+---------------------------+-------------------------------------------------------------+
| 1. Enhanced Process Mgmt  | kernel/proc.h, kernel/proc.c, kernel/syscall.h/c, user/ps.c |
| 2. MLFQ Scheduling        | kernel/proc.h, kernel/proc.c, kernel/trap.c                 |
| 3. System Call Tracing    | kernel/proc.h, kernel/proc.c, kernel/syscall.c, user/trace.c|
| 4. Copy-on-Write (COW)    | kernel/riscv.h, kernel/kalloc.c, kernel/vm.c, kernel/trap.c |
| 5. Kernel Statistics      | kernel/kalloc.c, kernel/proc.c, kernel/trap.c, user/kstats.c|
| 6. Benchmarking Suite     | user/schedbench.c, user/cowbench.c, Makefile                |
+---------------------------+-------------------------------------------------------------+
```

---

### Feature 1: Enhanced Process Management
**Objective**: Introduce process table inspection, detailed state retrieval, CPU time accounting, and a user-level `ps` utility.

#### Files & Functions to Modify:
- `kernel/proc.h`:
  - Add fields to `struct proc`:
    - `uint64 creation_time`: Tick count at creation.
    - `uint64 cpu_ticks`: Total CPU time consumed.
    - `uint num_sched`: Total times scheduled.
  - Define user-facing structure `struct proc_info` or `struct pstat` containing PID, PPID, state, size, name, and runtime stats.
- `kernel/proc.c`:
  - `allocproc()`: Initialize accounting fields (`creation_time = ticks`, `cpu_ticks = 0`, `num_sched = 0`).
  - `scheduler()`: Increment `p->num_sched` upon dispatch.
  - Implement `proc_getpinfo(uint64 addr)`: Scans `proc[]` table and copies process summary array to user memory via `copyout`.
- `kernel/syscall.h` & `kernel/syscall.c`:
  - Define `SYS_getpinfo 23`.
  - Add `[SYS_getpinfo] sys_getpinfo` to `syscalls[]` table.
- `kernel/sysproc.c`:
  - Implement `sys_getpinfo(void)`. Unpacks user destination pointer using `argaddr(0, &addr)` and calls `proc_getpinfo(addr)`.
- `user/user.h` & `user/usys.pl`:
  - Add `int getpinfo(struct proc_info *);` declaration and `entry("getpinfo");`.
- `user/ps.c` [NEW]:
  - User-space CLI utility calling `getpinfo` and printing PID, PPID, STATE, SIZE, TICKS, and NAME in formatted columns.
- `Makefile`:
  - Add `$U/_ps` to `UPROGS`.

---

### Feature 2: MLFQ CPU Scheduling
**Objective**: Replace default Round-Robin scheduler with a Multi-Level Feedback Queue (MLFQ) having 3 priority queues (Q0, Q1, Q2), dynamic time slices, priority demotion on CPU saturation, and periodic priority boosting to prevent starvation.

#### Scheduling Rules:
- **Rule 1**: If Priority(A) < Priority(B), A runs (lower number = higher priority; Q0 highest).
- **Rule 2**: If Priority(A) == Priority(B), Round-Robin within queue.
- **Rule 3**: Time slices: Q0 = 1 tick, Q1 = 2 ticks, Q2 = 4 ticks.
- **Rule 4**: If process uses up its time slice, it is demoted to next lower queue ($Q_i \to Q_{i+1}$). If it voluntarily relinquishes the CPU (I/O sleep before slice expires), it retains its priority level.
- **Rule 5**: Anti-starvation Priority Boost: Every `STARVATION_TICKS` (e.g. 50 ticks), boost all processes to Q0.

#### Files & Functions to Modify:
- `kernel/proc.h`:
  - Add scheduling fields to `struct proc`:
    - `int priority`: Current queue level (0, 1, or 2).
    - `int slice_ticks`: Ticks accumulated in current time slice.
    - `int total_ticks[3]`: Statistics tracking runtime in each queue.
- `kernel/proc.c`:
  - `allocproc()`: Set initial `p->priority = 0`, `p->slice_ticks = 0`.
  - `scheduler()`:
    - Search for `RUNNABLE` processes starting at Q0, then Q1, then Q2.
    - Pick highest-priority runnable process and context switch.
  - `yield()`: Reset or preserve `slice_ticks` based on whether quantum was fully consumed.
  - Add `mlfq_boost()`: Called periodically to reset all active processes to Q0 and reset `slice_ticks = 0`.
- `kernel/trap.c`:
  - `clockintr()`:
    - Check if `ticks % STARVATION_TICKS == 0` -> call `mlfq_boost()`.
  - `usertrap()`:
    - On timer interrupt (`which_dev == 2`):
      - Increment `p->slice_ticks`.
      - Check if `p->slice_ticks >= queue_quantum[p->priority]`.
      - If quantum expired: demote process (`if (p->priority < 2) p->priority++;`), reset `p->slice_ticks = 0`, and call `yield()`.
      - If quantum has not expired: do NOT yield yet, let process continue running!

---

### Feature 3: System Call Tracing
**Objective**: Implement a dynamic tracing framework (`trace` system call) that prints system call names, arguments, and return values for any process whose trace bitmask matches the system call. The trace mask must be inherited across `fork()`.

#### Files & Functions to Modify:
- `kernel/proc.h`:
  - Add `int trace_mask` to `struct proc`.
- `kernel/proc.c`:
  - `allocproc()`: Initialize `p->trace_mask = 0`.
  - `kfork()`: Inherit trace mask to child: `np->trace_mask = p->trace_mask`.
- `kernel/syscall.h` & `kernel/syscall.c`:
  - Add `#define SYS_trace 24`.
  - Define table of system call names `static char *syscall_names[]`.
  - Modify `syscall()`:
    ```c
    void syscall(void) {
      int num = p->trapframe->a7;
      if (num > 0 && num < NELEM(syscalls) && syscalls[num]) {
        p->trapframe->a0 = syscalls[num]();
        if ((p->trace_mask & (1 << num)) != 0) {
          printk("%d: syscall %s -> %ld\n", p->pid, syscall_names[num], p->trapframe->a0);
        }
      }
    }
    ```
- `kernel/sysproc.c`:
  - Implement `sys_trace(void)`: Unpacks integer mask with `argint(0, &mask)` and assigns `myproc()->trace_mask = mask`.
- `user/user.h` & `user/usys.pl`:
  - Add `int trace(int);` and `entry("trace");`.
- `user/trace.c` [NEW]:
  - CLI utility `trace <mask> <command> [args...]` that calls `trace(atoi(argv[1]))` and then `exec(argv[2], &argv[2])`.
- `Makefile`:
  - Add `$U/_trace` to `UPROGS`.

---

### Feature 4: Copy-on-Write (COW) Memory Management
**Objective**: Eliminate wasteful memory copying during `fork()`. Parent and child share the same physical pages read-only. Allocate and copy a page only when either process attempts to write to it.

```
Parent Page Table                    Physical Memory                    Child Page Table
+------------------+                +---------------+                 +------------------+
| VPN -> PA (RO)   | -------------> | Physical Page | <-------------- | VPN -> PA (RO)   |
| PTE_COW bit set  |                | refcnt = 2    |                 | PTE_COW bit set  |
| PTE_W cleared    |                +---------------+                 | PTE_W cleared    |
+------------------+                                                  +------------------+
                                           |
                              Write fault on Child (scause 15)
                                           v
+------------------+                +---------------+                 +------------------+
| VPN -> PA (RO)   | -------------> | Original Page |                 | VPN -> New PA    |
| refcnt drops to 1|                | refcnt = 1    |                 | PTE_W restored   |
+------------------+                +---------------+                 | PTE_COW cleared  |
                                                                      +------------------+
                                                                               |
                                                                               v
                                                                      +---------------+
                                                                      | New Page (RW) |
                                                                      | refcnt = 1    |
                                                                      +---------------+
```

#### Files & Functions to Modify:
- `kernel/riscv.h`:
  - Define `#define PTE_COW (1L << 8)` using bit 8 (RSW field in RISC-V Sv39 PTE).
- `kernel/kalloc.c`:
  - Define physical page reference count array and spinlock:
    ```c
    #define PA2INDEX(pa) (((uint64)(pa) - KERNBASE) / PGSIZE)
    struct {
      struct spinlock lock;
      int count[(PHYSTOP - KERNBASE) / PGSIZE];
    } kref;
    ```
  - Implement reference counting helpers:
    - `kref_inc(pa)`: Acquires `kref.lock`, increments `kref.count[PA2INDEX(pa)]`, releases lock.
    - `kref_dec(pa)`: Decrements count, returns remaining count.
    - `kref_get(pa)`: Returns current count.
  - `kinit()`: Initialize `kref.lock`.
  - `kalloc()`: Initialize `kref.count[PA2INDEX(pa)] = 1`.
  - `kfree(pa)`:
    - Decrement reference count under `kref.lock`.
    - Only if remaining count is <= 0 does it proceed to zero memory and push to `kmem.freelist`.
- `kernel/defs.h`:
  - Export `kref_inc`, `kref_dec`, `kref_get`, and `cowfault`.
- `kernel/vm.c`:
  - `uvmcopy(old, new, sz)`:
    - Stop allocating new pages with `kalloc()`.
    - For each page:
      - Clear `PTE_W`, set `PTE_COW`.
      - Install mapping in `new` pointing to same `pa`.
      - Call `kref_inc((void *)pa)`.
      - Update parent's PTE: clear `PTE_W`, set `PTE_COW`.
    - Call `sfence_vma()` to flush parent's TLB.
  - Implement `cowfault(pagetable, va)`:
    - Validates `va < p->sz`.
    - Finds PTE via `walk(pagetable, va, 0)`.
    - Verifies `*pte & PTE_V` and `*pte & PTE_COW`.
    - If `kref_get(pa) == 1`: process is sole owner. Clear `PTE_COW`, set `PTE_W`, flush TLB with `sfence_vma()`, return 0.
    - If `kref_get(pa) > 1`: allocate new page via `kalloc()`, copy contents with `memmove`, decrement refcount of old `pa` via `kref_dec`, update PTE to point to new page with `PTE_W` set and `PTE_COW` cleared, flush TLB with `sfence_vma()`, return 0.
  - `copyout(pagetable, psz, dstva, src, len)`:
    - **CRITICAL**: Detect if destination page has `PTE_COW` set before writing!
    - Call `cowfault(pagetable, va0)` so kernel writes into private physical memory rather than shared COW memory.
- `kernel/trap.c`:
  - `usertrap()`:
    - Detect store page fault: `r_scause() == 15`.
    - Check if address `r_stval()` is a COW page:
      ```c
      if (r_scause() == 15 && is_cow_page(p->pagetable, r_stval())) {
        if (cowfault(p->pagetable, r_stval()) < 0)
          setkilled(p);
      }
      ```
- `user/cowtest.c` [NEW]:
  - Test suite checking basic COW sharing, write faults, disk-to-memory write buffer copies, and memory reclamation.

---

### Feature 5: Kernel Statistics
**Objective**: Provide a real-time introspection system call (`kstats`) reporting memory consumption, page allocations, context switch counts, and page fault counters.

#### Files & Functions to Modify:
- `kernel/kstats.h` [NEW]:
  - Struct definition:
    ```c
    struct kstats {
      uint64 total_mem;       // Total physical RAM (128 MB)
      uint64 free_mem;        // Available physical memory in bytes
      uint64 num_procs;       // Number of active processes
      uint64 num_sched;       // Total context switches across all CPUs
      uint64 num_syscalls;    // Total system calls handled
      uint64 num_cow_faults;  // Total COW page faults handled
      uint64 num_lazy_faults; // Total lazy sbrk page faults handled
      uint64 uptime_ticks;    // Timer ticks since boot
    };
    ```
- `kernel/kalloc.c`:
  - Maintain `uint64 free_pages_count`.
  - Decrement on `kalloc()`, increment on `kfree()`.
- `kernel/proc.c`:
  - Global context switch counter `uint64 total_context_switches` incremented in `scheduler()`.
  - Active process counter `uint64 active_procs_count` updated in `allocproc()` and `freeproc()`.
- `kernel/trap.c`:
  - Counters `cow_fault_count` and `lazy_fault_count` incremented on successful page fault resolution.
- `kernel/syscall.c`:
  - Counter `syscall_count` incremented in `syscall()`.
  - Define `SYS_kstats 25`.
- `kernel/sysproc.c`:
  - Implement `sys_kstats(void)`: Populates `struct kstats` and copies to user memory via `copyout`.
- `user/kstats.c` [NEW]:
  - CLI utility displaying formatted system stats (memory free/total, active processes, faults, syscalls).
- `Makefile`:
  - Add `$U/_kstats` to `UPROGS`.

---

### Feature 6: Benchmarking Suite
**Objective**: Build rigorous benchmark programs to quantify scheduler performance and memory efficiency.

#### Files & Programs:
- `user/schedbench.c` [NEW]:
  - Spawns a balanced mix of CPU-bound processes (intensive math loops) and I/O-bound processes (periodic `pause(1)` calls).
  - Measures turnaround time, waiting time, and CPU utilization.
  - Used to contrast Round-Robin versus MLFQ performance.
- `user/cowbench.c` [NEW]:
  - Measures latency of repeated `fork()` operations with 1 MB, 4 MB, and 8 MB allocations.
  - Measures memory footprint before and after fork (querying `kstats`).
  - Compares execution time of read-only children versus children modifying pages.
- `Makefile`:
  - Add `$U/_schedbench` and `$U/_cowbench` to `UPROGS`.

---

## Part 4: Implementation Roadmap & Milestones

Following Rule 11 and Development Strategy from `PROJECT.md` (*Feature → Build → Test → Explain → Commit → Next Feature*):

```
+---------------------------------------------------------------------------------------+
| Milestone 1: Enhanced Process Management & System Call Tracing                        |
| - Implement sys_trace, syscall tracing in kernel/syscall.c, user/trace.c             |
| - Implement struct proc accounting, sys_getpinfo, and user/ps.c                       |
| - Verify with usertests and trace tests -> Git Commit                                |
+---------------------------------------------------------------------------------------+
                                           |
                                           v
+---------------------------------------------------------------------------------------+
| Milestone 2: Kernel Statistics & Benchmarking Scaffolding                            |
| - Implement global kernel counters (memory, faults, switches, syscalls)               |
| - Implement sys_kstats and user/kstats.c                                             |
| - Implement user/schedbench.c and user/cowbench.c baseline -> Git Commit              |
+---------------------------------------------------------------------------------------+
                                           |
                                           v
+---------------------------------------------------------------------------------------+
| Milestone 3: Multi-Level Feedback Queue (MLFQ) Scheduling                             |
| - Implement 3 queues, time slice tracking in struct proc                              |
| - Update scheduler() priority search and clockintr() quantum enforcement              |
| - Implement starvation boost mechanism                                                |
| - Run schedbench to compare with RR baseline -> Git Commit                           |
+---------------------------------------------------------------------------------------+
                                           |
                                           v
+---------------------------------------------------------------------------------------+
| Milestone 4: Copy-on-Write (COW) Memory Management                                    |
| - Implement physical reference counting in kernel/kalloc.c                            |
| - Implement PTE_COW bit, modify uvmcopy() and copyout() in kernel/vm.c                |
| - Implement cowfault() trap handling in kernel/trap.c                                 |
| - Validate with user/cowtest.c, usertests, and cowbench -> Git Commit                |
+---------------------------------------------------------------------------------------+
```

---

## Technical Notes & Invariants

> **PTE Bit Allocation for Copy-on-Write**:
> RISC-V Sv39 PTE bits 8 and 9 are Reserved for Software (RSW). We define `#define PTE_COW (1L << 8)`. This bit is preserved by `PTE_FLAGS (0x3FF)` and will not trigger hardware page fault errors directly, but clearing `PTE_W` ensures the CPU will generate a store page fault (`scause == 15`), allowing the kernel to catch it and perform the COW copy.

> **`copyout` Trap in COW**:
> When a user program issues a syscall like `read(fd, buf, len)` where `buf` points to a COW page, the kernel writes into `buf` using `copyout()`. If `copyout()` only checks `(*pte & PTE_W) == 0`, it will fail. `copyout()` must explicitly check for `PTE_COW` and duplicate the page before performing `memmove()`.

> **Preserving Existing Tests**:
> The repository already has `test-xv6.py` and `usertests`. Every feature implementation will be verified against `make qemu` and `python3 test-xv6.py -q usertests` to ensure zero regressions before committing.

