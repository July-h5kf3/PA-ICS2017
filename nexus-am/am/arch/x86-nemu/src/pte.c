#include <x86.h>
#include <klib.h>

#define PG_ALIGN __attribute((aligned(PGSIZE)))

static PDE kpdirs[NR_PDE] PG_ALIGN;
static PTE kptabs[PMEM_SIZE / PGSIZE] PG_ALIGN;
static void* (*palloc_f)();
static void (*pfree_f)(void*);

_Area segments[] = {      // Kernel memory mappings
  {.start = (void*)0,          .end = (void*)PMEM_SIZE}
};

#define NR_KSEG_MAP (sizeof(segments) / sizeof(segments[0]))

void _pte_init(void* (*palloc)(), void (*pfree)(void*)) {
  palloc_f = palloc;
  pfree_f = pfree;

  int i;

  // make all PDEs invalid
  for (i = 0; i < NR_PDE; i ++) {
    kpdirs[i] = 0;
  }

  PTE *ptab = kptabs;
  for (i = 0; i < NR_KSEG_MAP; i ++) {
    uint32_t pdir_idx = (uintptr_t)segments[i].start / (PGSIZE * NR_PTE);
    uint32_t pdir_idx_end = (uintptr_t)segments[i].end / (PGSIZE * NR_PTE);
    for (; pdir_idx < pdir_idx_end; pdir_idx ++) {
      // fill PDE
      kpdirs[pdir_idx] = (uintptr_t)ptab | PTE_P;

      // fill PTE
      PTE pte = PGADDR(pdir_idx, 0, 0) | PTE_P;
      PTE pte_end = PGADDR(pdir_idx + 1, 0, 0) | PTE_P;
      for (; pte < pte_end; pte += PGSIZE) {
        *ptab = pte;
        ptab ++;
      }
    }
  }

  set_cr3(kpdirs);
  set_cr0(get_cr0() | CR0_PG);
}

void _protect(_Protect *p) {
  PDE *updir = (PDE*)(palloc_f());
  p->ptr = updir;
  // map kernel space
  for (int i = 0; i < NR_PDE; i ++) {
    updir[i] = kpdirs[i];
  }

  p->area.start = (void*)0x8000000;
  p->area.end = (void*)0xc0000000;
}

void _release(_Protect *p) {
}

void _switch(_Protect *p) {
  set_cr3(p->ptr);
}

void _map(_Protect *p, void *va, void *pa) {
  assert((uintptr_t)va % PGSIZE == 0);
  assert((uintptr_t)pa % PGSIZE == 0);

  PDE *updir = (PDE *)p->ptr;
  uint32_t pdx = PDX(va);
  uint32_t ptx = PTX(va);

  if ((updir[pdx] & PTE_P) == 0) {
    PTE *ptab = (PTE *)palloc_f();
    assert(ptab != NULL);
    for (int i = 0; i < NR_PTE; i ++) {
      ptab[i] = 0;
    }
    updir[pdx] = PTE_ADDR(ptab) | PTE_P | PTE_W | PTE_U;
  }

  PTE *ptab = (PTE *)PTE_ADDR(updir[pdx]);
  ptab[ptx] = PTE_ADDR(pa) | PTE_P | PTE_W | PTE_U;
}

void _unmap(_Protect *p, void *va) {
}

_RegSet *_umake(_Protect *p, _Area ustack, _Area kstack, void *entry, char *const argv[], char *const envp[]) {
  (void)p;
  (void)kstack;
  (void)argv;
  (void)envp;

  uintptr_t *sp = (uintptr_t *)ustack.end;

  // Build a fake stack frame for _start(int argc, char **argv, char **envp).
  // After iret, esp will point here.
  *--sp = 0;  // fake return address, _start() never returns
  *--sp = 0;  // argc
  *--sp = 0;  // argv
  *--sp = 0;  // envp

  _RegSet *tf = (_RegSet *)((uintptr_t)sp - sizeof(_RegSet));
  memset(tf, 0, sizeof(*tf));

  tf->eip = (uintptr_t)entry;
  tf->cs = KSEL(SEG_KCODE);
  tf->eflags = FL_IF;
  tf->esp = (uintptr_t)sp;

  return tf;
}
