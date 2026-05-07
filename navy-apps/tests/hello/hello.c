#include <unistd.h>
#include <stdio.h>
#include <string.h>

int main() {
  write(1, "Hello World!\n", 13);
  int i = 2;
  volatile int j = 0;
  char buf[128];
  while (1) {
    j ++;
    if (j == 10000) {
      sprintf(buf, "Hello World for the %dth time\n", i ++);
      write(1, buf, strlen(buf));
      j = 0;
    }
  }
  return 0;
}
