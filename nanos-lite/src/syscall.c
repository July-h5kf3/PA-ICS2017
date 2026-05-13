#include "common.h"
#include "memory.h"
#include "syscall.h"
#include "fs.h"

_RegSet* do_syscall(_RegSet *r) {
  uintptr_t a[4];
  a[0] = SYSCALL_ARG1(r);
  a[1] = SYSCALL_ARG2(r);
  a[2] = SYSCALL_ARG3(r);
  a[3] = SYSCALL_ARG4(r);
  switch (a[0]) {
    case SYS_none:
      r->eax = 1;
      return r;
    case SYS_exit:
      _halt(a[1]);
      return NULL;
    case SYS_open:
      r->eax = fs_open((const char *)a[1], a[2], a[3]);
      return r;
    case SYS_read:
      r->eax = fs_read(a[1], (void *)a[2], a[3]);
      return r;
    case SYS_write:
      r->eax = fs_write(a[1], (const void *)a[2], a[3]);
      return r;
    case SYS_close:
      r->eax = fs_close(a[1]);
      return r;
    case SYS_lseek:
      r->eax = fs_lseek(a[1], a[2], a[3]);
      return r;
    case SYS_brk:
      r->eax = mm_brk(a[1]);
      return r;
    default: panic("Unhandled syscall ID = %d", a[0]);
  }

  return NULL;
}
