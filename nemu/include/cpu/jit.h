#ifndef __CPU_JIT_H__
#define __CPU_JIT_H__

#include "common.h"

void jit_init(void);
uint32_t jit_exec(uint64_t limit);

#endif
