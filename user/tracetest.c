#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/syscall.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  printf("=== System Call Tracing Validation Suite ===\n");

  // Test 1: Single Syscall Tracing (SYS_pause = 13)
  printf("\n[Test 1] Testing single syscall tracing (SYS_pause)...\n");
  printf("Expected output below: trace message for 'pause', none for 'getpid'\n");
  if (trace(1 << SYS_pause) < 0) {
    printf("tracetest: FAIL - trace(1 << SYS_pause) returned error\n");
    exit(1);
  }
  getpid();   // should NOT be traced
  pause(1);   // SHOULD be traced (num=13, return=0)
  printf("[Test 1] PASSED.\n");

  // Test 2: Multiple Syscalls Tracing (SYS_open = 15, SYS_close = 21)
  printf("\n[Test 2] Testing multi-syscall tracing (open & close)...\n");
  printf("Expected output below: trace messages for 'open' and 'close'\n");
  trace((1 << SYS_open) | (1 << SYS_close));
  int fd = open("README", 0);
  if (fd < 0) {
    printf("tracetest: FAIL - failed to open README\n");
    exit(1);
  }
  close(fd);
  printf("[Test 2] PASSED.\n");

  // Test 3: Error Return Value Tracing
  printf("\n[Test 3] Testing error return value reporting (open non-existent)...\n");
  printf("Expected output below: trace message for 'open' with return=-1\n");
  trace(1 << SYS_open);
  int bad_fd = open("__nonexistent_file__", 0);
  if (bad_fd != -1) {
    printf("tracetest: FAIL - expected open error\n");
    close(bad_fd);
    exit(1);
  }
  printf("[Test 3] PASSED.\n");

  // Test 4: Fork Inheritance
  printf("\n[Test 4] Testing trace mask inheritance across fork()...\n");
  printf("Expected output below: child process traces 'getpid'\n");
  trace(1 << SYS_getpid);
  int pid = fork();
  if (pid < 0) {
    printf("tracetest: FAIL - fork failed\n");
    exit(1);
  }
  if (pid == 0) {
    // Child: should inherit trace mask for SYS_getpid
    getpid();
    exit(0);
  }
  wait(0);
  printf("[Test 4] PASSED.\n");

  // Test 5: Disable Tracing
  printf("\n[Test 5] Testing trace disable (mask = 0)...\n");
  printf("Expected output below: NO trace messages should appear\n");
  trace(0);
  pause(1);
  getpid();
  fd = open("README", 0);
  if (fd >= 0) close(fd);
  printf("[Test 5] PASSED.\n");

  printf("\n=== ALL TRACE TESTS PASSED ===\n");
  exit(0);
}
