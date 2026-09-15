#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  if (argc < 3) {
    fprintf(2, "Usage: trace <mask> <command> [args...]\n");
    exit(1);
  }

  int mask = atoi(argv[1]);
  if (trace(mask) < 0) {
    fprintf(2, "trace: trace(%d) failed\n", mask);
    exit(1);
  }

  exec(argv[2], &argv[2]);
  fprintf(2, "trace: exec %s failed\n", argv[2]);
  exit(1);
}
