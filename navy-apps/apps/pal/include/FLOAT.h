#ifndef __FLOAT_H__
#define __FLOAT_H__

#include <stdint.h>
#include "assert.h"

#define FLOAT_FRAC_BITS 16
#define FLOAT_ONE       (1 << FLOAT_FRAC_BITS)

typedef int32_t FLOAT;

static inline int32_t F2int(FLOAT a) {
  return a >> FLOAT_FRAC_BITS;
}

static inline FLOAT int2F(int a) {
  return (FLOAT)((uint32_t)a << FLOAT_FRAC_BITS);
}

static inline FLOAT F_mul_int(FLOAT a, int b) {
  return (FLOAT)((uint32_t)a * (uint32_t)b);
}

static inline FLOAT F_div_int(FLOAT a, int b) {
  assert(b != 0);
  return a / b;
}

FLOAT f2F(float);
FLOAT F_mul_F(FLOAT, FLOAT);
FLOAT F_div_F(FLOAT, FLOAT);
FLOAT Fabs(FLOAT);
FLOAT Fsqrt(FLOAT);
FLOAT Fpow(FLOAT, FLOAT);

#endif
