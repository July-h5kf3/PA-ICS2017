#include "cpu/exec.h"

static inline uint32_t rot_mask(int width) {
  return width == 4 ? 0xffffffffu : ((1u << (width * 8)) - 1);
}

make_EHelper(test) {
  // TODO();
  rtl_and(&t2,&id_dest->val,&id_src->val);
  rtl_update_ZFSF(&t2,id_dest->width);
  rtl_set_CF(&tzero);
  rtl_set_OF(&tzero);

  print_asm_template2(test);
}

make_EHelper(and) {
  // TODO();
  rtl_and(&t2,&id_dest->val,&id_src->val);
  operand_write(id_dest,&t2);
  rtl_update_ZFSF(&t2,id_dest->width);
  rtl_set_CF(&tzero);
  rtl_set_OF(&tzero);

  print_asm_template2(and);
}

make_EHelper(xor) {
  // TODO();
  rtl_xor(&t2,&id_dest->val,&id_src->val);
  operand_write(id_dest,&t2);
  rtl_update_ZFSF(&t2,id_dest->width);
  rtl_set_CF(&tzero);
  rtl_set_OF(&tzero);

  print_asm_template2(xor);
}

make_EHelper(or) {
  // TODO();
  rtl_or(&t2,&id_dest->val,&id_src->val);
  operand_write(id_dest,&t2);
  rtl_update_ZFSF(&t2,id_dest->width);
  rtl_set_CF(&tzero);
  rtl_set_OF(&tzero);
  print_asm_template2(or);
}

make_EHelper(sar) {
  rtl_sar(&t2, &id_dest->val, &id_src->val);
  operand_write(id_dest, &t2);
  rtl_update_ZFSF(&t2, id_dest->width);
  // unnecessary to update CF and OF in NEMU

  print_asm_template2(sar);
}

make_EHelper(shl) {
  rtl_shl(&t2, &id_dest->val, &id_src->val);
  operand_write(id_dest, &t2);
  rtl_update_ZFSF(&t2, id_dest->width);
  // unnecessary to update CF and OF in NEMU

  print_asm_template2(shl);
}

make_EHelper(shr) {
  rtl_shr(&t2, &id_dest->val, &id_src->val);
  operand_write(id_dest, &t2);
  rtl_update_ZFSF(&t2, id_dest->width);
  // unnecessary to update CF and OF in NEMU

  print_asm_template2(shr);
}

make_EHelper(rol) {
  int bits = id_dest->width * 8;
  uint32_t mask = rot_mask(id_dest->width);
  uint32_t count = id_src->val & 0x1f;
  uint32_t n = count % bits;
  uint32_t val = id_dest->val & mask;

  if (n != 0) {
    uint32_t res = ((val << n) | (val >> (bits - n))) & mask;
    id_dest->val = res;
    operand_write(id_dest, &id_dest->val);

    cpu.Eflags.CF = res & 1;
    if (n == 1) {
      cpu.Eflags.OF = ((res >> (bits - 1)) & 1) ^ cpu.Eflags.CF;
    }
  }

  print_asm_template2(rol);
}

make_EHelper(ror) {
  int bits = id_dest->width * 8;
  uint32_t mask = rot_mask(id_dest->width);
  uint32_t count = id_src->val & 0x1f;
  uint32_t n = count % bits;
  uint32_t val = id_dest->val & mask;

  if (n != 0) {
    uint32_t res = ((val >> n) | (val << (bits - n))) & mask;
    id_dest->val = res;
    operand_write(id_dest, &id_dest->val);

    cpu.Eflags.CF = (res >> (bits - 1)) & 1;
    if (n == 1) {
      cpu.Eflags.OF = ((res >> (bits - 1)) & 1) ^ ((res >> (bits - 2)) & 1);
    }
  }

  print_asm_template2(ror);
}

make_EHelper(rcl) {
  int bits = id_dest->width * 8;
  uint32_t mask = rot_mask(id_dest->width);
  uint32_t count = id_src->val & 0x1f;
  uint32_t n = count % (bits + 1);
  uint32_t val = id_dest->val & mask;

  if (n != 0) {
    uint32_t cf = cpu.Eflags.CF;
    for (uint32_t i = 0; i < n; i++) {
      uint32_t new_cf = (val >> (bits - 1)) & 1;
      val = ((val << 1) | cf) & mask;
      cf = new_cf;
    }

    id_dest->val = val;
    operand_write(id_dest, &id_dest->val);

    cpu.Eflags.CF = cf;
    if (n == 1) {
      cpu.Eflags.OF = ((val >> (bits - 1)) & 1) ^ cpu.Eflags.CF;
    }
  }

  print_asm_template2(rcl);
}

make_EHelper(rcr) {
  int bits = id_dest->width * 8;
  uint32_t mask = rot_mask(id_dest->width);
  uint32_t count = id_src->val & 0x1f;
  uint32_t n = count % (bits + 1);
  uint32_t val = id_dest->val & mask;

  if (n != 0) {
    uint32_t cf = cpu.Eflags.CF;
    for (uint32_t i = 0; i < n; i++) {
      uint32_t new_cf = val & 1;
      val = (val >> 1) | (cf << (bits - 1));
      val &= mask;
      cf = new_cf;
    }

    id_dest->val = val;
    operand_write(id_dest, &id_dest->val);

    cpu.Eflags.CF = cf;
    if (n == 1) {
      cpu.Eflags.OF = ((val >> (bits - 1)) & 1) ^ ((val >> (bits - 2)) & 1);
    }
  }

  print_asm_template2(rcr);
}

make_EHelper(setcc) {
  uint8_t subcode = decoding.opcode & 0xf;
  rtl_setcc(&t2, subcode);
  operand_write(id_dest, &t2);

  print_asm("set%s %s", get_cc_name(subcode), id_dest->str);
}

make_EHelper(not) {
  rtl_mv(&t2, &id_dest->val);
  rtl_not(&t2);
  operand_write(id_dest, &t2);

  print_asm_template1(not);
}
