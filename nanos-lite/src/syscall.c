#include "common.h"
#include "syscall.h"

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
    default: panic("Unhandled syscall ID = %d", a[0]);
    case SYS_write:
      if(a[1] == 1 || a[1] == 2)
      {
        for(int i = 0; i < a[3]; i++)
        {
          _putc(((char*)a[2])[i]);
        }
        r->eax = a[3];
        return r;
      }
      else
      {
        r->eax = -1;
        return r;
      }
  }

  return NULL;
}
