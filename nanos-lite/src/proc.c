#include "proc.h"

#define MAX_NR_PROC 4
#define USTACK_TOP  ((uintptr_t)0xc0000000)

static PCB pcb[MAX_NR_PROC];
static int nr_proc = 0;
PCB *current = NULL;

uintptr_t loader(_Protect *as, const char *filename);

void load_prog(const char *filename) {
  int i = nr_proc ++;
  _protect(&pcb[i].as);

  uintptr_t entry = loader(&pcb[i].as, filename);

  uintptr_t ustack_bottom = USTACK_TOP - STACK_SIZE;
  for (uintptr_t va = ustack_bottom; va < USTACK_TOP; va += PGSIZE) {
    void *pa = new_page();
    memset(pa, 0, PGSIZE);
    _map(&pcb[i].as, (void *)va, pa);
  }

  _Area stack;
  stack.start = pcb[i].stack;
  stack.end = stack.start + sizeof(pcb[i].stack);

  pcb[i].tf = _umake(&pcb[i].as, stack, stack, (void *)entry, NULL, NULL);
  current = &pcb[i];
}

_RegSet* schedule(_RegSet *prev) {
  if (current != NULL) {
    current->tf = prev;
  }
  current = &pcb[0];
  _switch(&current->as);
  return current->tf;
}
