#include "nemu.h"
#include "device/mmio.h"

#define PMEM_SIZE (128 * 1024 * 1024)

#define pmem_rw(addr, type) *(type *)({\
    Assert(addr < PMEM_SIZE, "physical address(0x%08x) is out of bound", addr); \
    guest_to_host(addr); \
    })

uint8_t pmem[PMEM_SIZE];

static inline bool is_page_enabled(void) {
  return (cpu.cr0.val & 0x80000001) == 0x80000001;
}

static paddr_t page_translate(vaddr_t addr, bool is_write) {
  CR3 cr3 = cpu.cr3;
  paddr_t pgdir_base = cr3.page_directory_base << 12;

  paddr_t pde_addr = pgdir_base + (((addr >> 22) & 0x3ff) << 2);
  PDE pde;
  pde.val = paddr_read(pde_addr, 4);
  Assert(pde.present, "PDE not present for vaddr 0x%08x", addr);
  if (!pde.accessed) {
    pde.accessed = 1;
    paddr_write(pde_addr, 4, pde.val);
  }

  paddr_t pte_addr = (pde.page_frame << 12) + (((addr >> 12) & 0x3ff) << 2);
  PTE pte;
  pte.val = paddr_read(pte_addr, 4);
  Assert(pte.present, "PTE not present for vaddr 0x%08x", addr);
  if (!pte.accessed || (is_write && !pte.dirty)) {
    pte.accessed = 1;
    if (is_write) {
      pte.dirty = 1;
    }
    paddr_write(pte_addr, 4, pte.val);
  }

  return (pte.page_frame << 12) | (addr & PAGE_MASK);
}

/* Memory accessing interfaces */

uint32_t paddr_read(paddr_t addr, int len) {
  int map_NO = is_mmio(addr);
  if (map_NO != -1) {
    return mmio_read(addr, len, map_NO);
  }
  return pmem_rw(addr, uint32_t) & (~0u >> ((4 - len) << 3));
}

void paddr_write(paddr_t addr, int len, uint32_t data) {
  int map_NO = is_mmio(addr);
  if (map_NO != -1) {
    mmio_write(addr, len, data, map_NO);
    return;
  }
  memcpy(guest_to_host(addr), &data, len);
}

uint32_t vaddr_read(vaddr_t addr, int len) {
  if (!is_page_enabled()) {
    return paddr_read(addr, len);
  }

  if ((addr & ~PAGE_MASK) != ((addr + len - 1) & ~PAGE_MASK)) {
    int len1 = PAGE_SIZE - (addr & PAGE_MASK);
    int len2 = len - len1;
    uint32_t low = paddr_read(page_translate(addr, false), len1);
    uint32_t high = paddr_read(page_translate(addr + len1, false), len2);
    return low | (high << (len1 << 3));
  }

  return paddr_read(page_translate(addr, false), len);
}

void vaddr_write(vaddr_t addr, int len, uint32_t data) {
  if (!is_page_enabled()) {
    paddr_write(addr, len, data);
    return;
  }

  if ((addr & ~PAGE_MASK) != ((addr + len - 1) & ~PAGE_MASK)) {
    int len1 = PAGE_SIZE - (addr & PAGE_MASK);
    int len2 = len - len1;
    paddr_write(page_translate(addr, true), len1, data);
    paddr_write(page_translate(addr + len1, true), len2, data >> (len1 << 3));
    return;
  }

  paddr_write(page_translate(addr, true), len, data);
}
