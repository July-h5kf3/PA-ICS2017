#include "common.h"
#include "fs.h"
#include "memory.h"

#define DEFAULT_ENTRY ((void *)0x8048000)

uintptr_t loader(_Protect *as, const char *filename, uintptr_t *brk) {
  int fd = fs_open(filename, 0, 0);
  size_t size = fs_filesz(fd);
  uintptr_t va = (uintptr_t)DEFAULT_ENTRY;
  size_t offset = 0;

  while (offset < size) {
    void *pa = new_page();
    _map(as, (void *)(va + offset), pa);

    size_t len = size - offset;
    if (len > PGSIZE) {
      len = PGSIZE;
    }

    memset(pa, 0, PGSIZE);
    fs_read(fd, pa, len);
    offset += len;
  }

  fs_close(fd);
  if (brk != NULL) {
    *brk = va + size;
  }
  return va;
}
