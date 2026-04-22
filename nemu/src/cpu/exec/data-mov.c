#include "cpu/exec.h"

make_EHelper(mov) {
  operand_write(id_dest, &id_src->val);
  print_asm_template2(mov);
}

make_EHelper(push) {
  rtl_push(&id_dest->val, decoding.is_operand_size_16 ? 2 : 4);

  print_asm_template1(push);
}

make_EHelper(pop) {
  rtl_pop(&id_dest->val, decoding.is_operand_size_16 ? 2 : 4);
  operand_write(id_dest, &id_dest->val);

  print_asm_template1(pop);
}

make_EHelper(pusha) {
  int width = decoding.is_operand_size_16 ? 2 : 4;
  rtlreg_t esp = cpu.esp;

  rtl_push(&cpu.eax, width);
  rtl_push(&cpu.ecx, width);
  rtl_push(&cpu.edx, width);
  rtl_push(&cpu.ebx, width);
  rtl_push(&esp, width);
  rtl_push(&cpu.ebp, width);
  rtl_push(&cpu.esi, width);
  rtl_push(&cpu.edi, width);

  print_asm("pusha");
}

make_EHelper(popa) {
  TODO();

  print_asm("popa");
}

make_EHelper(leave) {
  int width = decoding.is_operand_size_16 ? 2 : 4;

  rtl_lr(&t0, R_EBP, width);
  rtl_mv(&cpu.esp, &t0);
  rtl_pop(&t1, width);
  rtl_sr(R_EBP, width, &t1);

  print_asm("leave");
}

make_EHelper(cltd) {
  if (decoding.is_operand_size_16) {
    rtl_lr_w(&t0, R_AX);
    rtl_msb(&t0, &t0, 2);
    rtl_sub(&t0, &tzero, &t0);
    rtl_sr_w(R_DX, &t0);
  }
  else {
    rtl_lr_l(&t0, R_EAX);
    rtl_msb(&t0, &t0, 4);
    rtl_sub(&t0, &tzero, &t0);
    rtl_sr_l(R_EDX, &t0);
  }

  print_asm(decoding.is_operand_size_16 ? "cwtl" : "cltd");
}

make_EHelper(cwtl) {
  if (decoding.is_operand_size_16) {
    rtl_lr_b(&t0, R_AL);
    rtl_sext(&t0, &t0, 1);
    rtl_sr_w(R_AX, &t0);
  }
  else {
    rtl_lr_w(&t0, R_AX);
    rtl_sext(&t0, &t0, 2);
    rtl_sr_l(R_EAX, &t0);
  }

  print_asm(decoding.is_operand_size_16 ? "cbtw" : "cwtl");
}

make_EHelper(movsx) {
  id_dest->width = decoding.is_operand_size_16 ? 2 : 4;
  rtl_sext(&t2, &id_src->val, id_src->width);
  operand_write(id_dest, &t2);
  print_asm_template2(movsx);
}

make_EHelper(movzx) {
  id_dest->width = decoding.is_operand_size_16 ? 2 : 4;
  operand_write(id_dest, &id_src->val);
  print_asm_template2(movzx);
}

make_EHelper(lea) {
  rtl_li(&t2, id_src->addr);
  operand_write(id_dest, &t2);
  print_asm_template2(lea);
}

make_EHelper(xchg) {
  t0 = id_dest->val;
  id_dest->val = id_src->val;
  id_src->val = t0;
  operand_write(id_dest,&id_dest->val);
  operand_write(id_src,&id_src->val);
}
