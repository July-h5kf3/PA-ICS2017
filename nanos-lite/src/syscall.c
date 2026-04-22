#include "common.h"
#include "syscall.h"

_RegSet* do_syscall(_RegSet *r) {
  uintptr_t a[4];
  a[0] = SYSCALL_ARG1(r);
  a[1] = SYSCALL_ARG2(r);

  switch (a[0]) {
    case SYS_none:
      r->eax = 1;
      return r;
    case SYS_exit:
      _halt(a[1]);
      return NULL;
    default: panic("Unhandled syscall ID = %d", a[0]);
  }

  return NULL;
}
