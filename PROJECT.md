# Enhanced Operating System Kernel Based on xv6

## Project Goal

Extend the official MIT xv6-riscv educational operating system with
selected advanced operating-system features.

## Base System

MIT xv6-riscv.

## Environment

- Linux / WSL2
- RISC-V
- QEMU
- C
- Make
- GDB

## Planned Features

### Core Features

1. Enhanced Process Management
2. MLFQ CPU Scheduling
3. System Call Tracing
4. Copy-on-Write Memory Management

### Additional Features

5. Kernel Statistics
6. Scheduler and Memory Benchmarking

## Important Development Rules

1. Do not rewrite xv6 from scratch.
2. Preserve existing xv6 functionality.
3. Make minimal and understandable changes.
4. Do not modify unrelated files.
5. Do not blindly copy code from external repositories.
6. Follow existing xv6 coding conventions.
7. Compile after every significant modification.
8. Test every feature independently.
9. Explain all kernel-level changes.
10. Never proceed to the next feature if the current feature is broken.
11. Keep Git commits after every completed feature.

## Development Strategy

Feature → Build → Test → Explain → Commit → Next Feature

## Current Status

[x] WSL environment
[x] RISC-V compiler
[x] QEMU
[x] Original xv6 boots

[x] Enhanced process management
[ ] MLFQ scheduler
[ ] System call tracing
[ ] Copy-on-Write
[ ] Kernel statistics
[ ] Benchmarking
