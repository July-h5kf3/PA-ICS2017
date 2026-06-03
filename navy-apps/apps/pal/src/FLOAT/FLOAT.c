#include "FLOAT.h"
#include <stdint.h>
#include <assert.h>

static uint32_t
F_abs_u32(FLOAT a)
{
  uint32_t ua = (uint32_t)a;
  return (a < 0) ? (0u - ua) : ua;
}

static FLOAT
F_apply_sign(uint32_t a, int negative)
{
  return negative ? (FLOAT)(0u - a) : (FLOAT)a;
}

FLOAT F_mul_F(FLOAT a, FLOAT b) {
  return (FLOAT)(((int64_t)a * (int64_t)b) >> FLOAT_FRAC_BITS);
}

FLOAT F_div_F(FLOAT a, FLOAT b) {
  assert(b != 0);

  int negative = (a < 0) ^ (b < 0);
  uint32_t dividend = F_abs_u32(a);
  uint32_t divisor = F_abs_u32(b);
  uint32_t quotient = 0;
  uint32_t remainder = 0;

  for (int i = 31; i >= 0; i --) {
    remainder = (remainder << 1) | ((dividend >> i) & 1u);
    if (remainder >= divisor) {
      remainder -= divisor;
      if (i < FLOAT_FRAC_BITS) {
        quotient |= 1u << (i + FLOAT_FRAC_BITS);
      }
    }
  }

  for (int i = FLOAT_FRAC_BITS - 1; i >= 0; i --) {
    remainder <<= 1;
    if (remainder >= divisor) {
      remainder -= divisor;
      quotient |= 1u << i;
    }
  }

  return F_apply_sign(quotient, negative);
}

FLOAT f2F(float a) {
  /* You should figure out how to convert `a' into FLOAT without
   * introducing x87 floating point instructions. Else you can
   * not run this code in NEMU before implementing x87 floating
   * point instructions, which is contrary to our expectation.
   *
   * Hint: The bit representation of `a' is already on the
   * stack. How do you retrieve it to another variable without
   * performing arithmetic operations on it directly?
   */

  union {
    float f;
    uint32_t u;
  } bits = { .f = a };

  uint32_t sign = bits.u >> 31;
  uint32_t exponent = (bits.u >> 23) & 0xff;
  uint32_t mantissa = bits.u & 0x7fffff;

  if (exponent == 0) {
    return 0;
  }

  if (exponent == 0xff) {
    return sign ? (FLOAT)0x80000000u : (FLOAT)0x7fffffffu;
  }

  mantissa |= 1u << 23;
  int shift = (int)exponent - 127 - 23 + FLOAT_FRAC_BITS;
  uint32_t value;

  if (shift >= 8) {
    value = sign ? 0x80000000u : 0x7fffffffu;
  } else if (shift >= 0) {
    value = mantissa << shift;
  } else if (shift <= -32) {
    value = 0;
  } else {
    value = mantissa >> -shift;
  }

  return F_apply_sign(value, sign);
}

FLOAT Fabs(FLOAT a) {
  return (a < 0) ? -a : a;
}

/* Functions below are already implemented */

FLOAT Fsqrt(FLOAT x) {
  FLOAT dt, t = int2F(2);

  do {
    dt = F_div_int((F_div_F(x, t) - t), 2);
    t += dt;
  } while(Fabs(dt) > f2F(1e-4));

  return t;
}

FLOAT Fpow(FLOAT x, FLOAT y) {
  /* we only compute x^0.333 */
  FLOAT t2, dt, t = int2F(2);

  do {
    t2 = F_mul_F(t, t);
    dt = (F_div_F(x, t2) - t) / 3;
    t += dt;
  } while(Fabs(dt) > f2F(1e-4));

  return t;
}
