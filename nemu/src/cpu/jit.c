#include "cpu/jit.h"
#include "cpu/decode.h"
#include "cpu/reg.h"
#include "memory/memory.h"
#include "monitor/monitor.h"

#include <stddef.h>
#include <sys/mman.h>
#include <unistd.h>

#define JIT_ENABLE 1

#define JIT_CACHE_BITS 16
#define JIT_CACHE_SIZE (1u << JIT_CACHE_BITS)
#define JIT_CACHE_MASK (JIT_CACHE_SIZE - 1)

#define JIT_CODE_SIZE (16 * 1024 * 1024)
#define JIT_MAX_TB_INSNS 64

#define CPU_OFF_GPR(r) ((uint32_t)offsetof(CPU_state, gpr[(r)]._32))
#define CPU_OFF_EIP ((uint32_t)offsetof(CPU_state, eip))
typedef uint32_t (*tb_func_t)(uint64_t);

typedef struct {
  vaddr_t pc;
  uint8_t *code;
  uint32_t nr_insn;
} TB;

typedef struct {
  uint8_t *start;
  uint8_t *cur;
  uint8_t *end;
} CodeBuf;

typedef enum {
  JOP_MOV,
  JOP_LEA,
  JOP_ADD,
  JOP_SUB,
  JOP_AND,
  JOP_OR,
  JOP_XOR,
  JOP_CMP,
  JOP_TEST,
  JOP_INC,
  JOP_DEC,
  JOP_PUSH,
  JOP_POP,
  JOP_NOP,
  JOP_MOVZX,
  JOP_MOVSX,
  JOP_SHL,
  JOP_SHR,
  JOP_SAR,
  JOP_JMP,
  JOP_JCC,
  JOP_CALL,
  JOP_RET,
  JOP_SETCC,
  JOP_LEAVE,
  JOP_CWTL,
  JOP_CLTD,
} JitOp;

typedef enum {
  O_NONE,
  O_REG,
  O_IMM,
  O_MEM,
} OpType;

typedef struct {
  OpType type;
  int width;
  int reg;
  uint32_t imm;
  int32_t simm;
  int base;
  int index;
  int scale;
  int32_t disp;
} JOperand;

typedef struct {
  JitOp op;
  JOperand dst;
  JOperand src;
  uint32_t pc;
  uint32_t next_pc;
} JInstr;

typedef struct {
  JInstr insn[JIT_MAX_TB_INSNS];
  uint32_t nr;
  uint32_t pc;
  uint32_t end_pc;
} JBlock;

static TB jit_cache[JIT_CACHE_SIZE];
static CodeBuf codebuf;
static uint32_t jit_scratch[JIT_MAX_TB_INSNS * 4];
static uint64_t jit_hits, jit_misses, jit_fallbacks;

static inline uint32_t cache_index(vaddr_t pc) {
  return ((pc >> 1) ^ (pc >> 11)) & JIT_CACHE_MASK;
}

static uint32_t jit_vaddr_read(uint32_t addr, uint32_t len) {
  return vaddr_read(addr, len);
}

static void jit_vaddr_write(uint32_t addr, uint32_t len, uint32_t data) {
  vaddr_write(addr, len, data);
}

static uint32_t mask_width(uint32_t val, int width) {
  switch (width) {
    case 1: return val & 0xffu;
    case 2: return val & 0xffffu;
    case 4: return val;
    default: assert(0);
  }
}

static void jit_set_logic_flags(uint32_t result, uint32_t width) {
  result = mask_width(result, width);
  cpu.Eflags.ZF = (result == 0);
  cpu.Eflags.SF = (result >> (width * 8 - 1)) & 1;
  cpu.Eflags.CF = 0;
  cpu.Eflags.OF = 0;
}

static void jit_set_zfsf_flags(uint32_t result, uint32_t width) {
  result = mask_width(result, width);
  cpu.Eflags.ZF = (result == 0);
  cpu.Eflags.SF = (result >> (width * 8 - 1)) & 1;
}

static void jit_set_add_flags(uint32_t lhs, uint32_t rhs, uint32_t result, uint32_t width) {
  uint32_t mask = (width == 4) ? 0xffffffffu : ((1u << (width * 8)) - 1);
  lhs &= mask;
  rhs &= mask;
  result &= mask;
  cpu.Eflags.ZF = (result == 0);
  cpu.Eflags.SF = (result >> (width * 8 - 1)) & 1;
  cpu.Eflags.CF = (result < lhs);
  cpu.Eflags.OF = (((lhs ^ result) & (rhs ^ result)) >> (width * 8 - 1)) & 1;
}

static void jit_set_inc_flags(uint32_t lhs, uint32_t result, uint32_t width) {
  uint32_t mask = (width == 4) ? 0xffffffffu : ((1u << (width * 8)) - 1);
  lhs &= mask;
  result &= mask;
  cpu.Eflags.ZF = (result == 0);
  cpu.Eflags.SF = (result >> (width * 8 - 1)) & 1;
  cpu.Eflags.OF = ((~(lhs ^ 1u) & (lhs ^ result)) >> (width * 8 - 1)) & 1;
}

static void jit_set_dec_flags(uint32_t lhs, uint32_t result, uint32_t width) {
  uint32_t mask = (width == 4) ? 0xffffffffu : ((1u << (width * 8)) - 1);
  lhs &= mask;
  result &= mask;
  cpu.Eflags.ZF = (result == 0);
  cpu.Eflags.SF = (result >> (width * 8 - 1)) & 1;
  cpu.Eflags.OF = (((lhs ^ 1u) & (lhs ^ result)) >> (width * 8 - 1)) & 1;
}

static void jit_set_sub_flags(uint32_t lhs, uint32_t rhs, uint32_t result, uint32_t width) {
  uint32_t mask = (width == 4) ? 0xffffffffu : ((1u << (width * 8)) - 1);
  lhs &= mask;
  rhs &= mask;
  result &= mask;
  cpu.Eflags.ZF = (result == 0);
  cpu.Eflags.SF = (result >> (width * 8 - 1)) & 1;
  cpu.Eflags.CF = (lhs < rhs);
  cpu.Eflags.OF = (((lhs ^ rhs) & (lhs ^ result)) >> (width * 8 - 1)) & 1;
}

static uint32_t jit_eval_cc(uint32_t subcode) {
  bool invert = subcode & 0x1;
  uint32_t ret;

  switch (subcode & 0xe) {
    case 0x0: ret = cpu.Eflags.OF; break;
    case 0x2: ret = cpu.Eflags.CF; break;
    case 0x4: ret = cpu.Eflags.ZF; break;
    case 0x6: ret = cpu.Eflags.CF | cpu.Eflags.ZF; break;
    case 0x8: ret = cpu.Eflags.SF; break;
    case 0xc: ret = cpu.Eflags.SF ^ cpu.Eflags.OF; break;
    case 0xe: ret = cpu.Eflags.ZF | (cpu.Eflags.SF ^ cpu.Eflags.OF); break;
    default: panic("JIT does not support parity condition code");
  }

  return invert ? (ret ^ 1) : ret;
}

static void jit_set_jcc_eip(uint32_t subcode, uint32_t target, uint32_t fallthrough) {
  cpu.eip = jit_eval_cc(subcode) ? target : fallthrough;
}

static void jit_handle_irq(uint32_t ret_addr) {
  const uint8_t IRQ_TIMER = 32;
  extern void raise_intr(uint8_t NO, vaddr_t ret_addr);

  if (cpu.INTR && cpu.Eflags.IF) {
    cpu.INTR = false;
    raise_intr(IRQ_TIMER, ret_addr);
    if (decoding.is_jmp) {
      decoding.is_jmp = 0;
      cpu.eip = decoding.jmp_eip;
    }
  }
}

static void cb_need(size_t n) {
  if (codebuf.cur + n >= codebuf.end) {
    Assert(0, "JIT code cache overflow during translation");
  }
}

static void emit8(uint8_t v) {
  cb_need(1);
  *codebuf.cur++ = v;
}

static void emit32(uint32_t v) {
  cb_need(4);
  memcpy(codebuf.cur, &v, 4);
  codebuf.cur += 4;
}

static void emit64(uint64_t v) {
  cb_need(8);
  memcpy(codebuf.cur, &v, 8);
  codebuf.cur += 8;
}

enum {
  HR_RAX = 0,
  HR_RCX = 1,
  HR_RDX = 2,
  HR_RBX = 3,
  HR_RSP = 4,
  HR_RBP = 5,
  HR_RSI = 6,
  HR_RDI = 7,
  HR_R8  = 8,
  HR_R9  = 9,
  HR_R10 = 10,
  HR_R11 = 11,
  HR_R12 = 12,
  HR_R13 = 13,
  HR_R14 = 14,
  HR_R15 = 15,
};

static void rex(bool w, int r, int x, int b) {
  uint8_t v = 0x40;
  if (w) v |= 0x08;
  if (r & 8) v |= 0x04;
  if (x & 8) v |= 0x02;
  if (b & 8) v |= 0x01;
  if (v != 0x40) emit8(v);
}

static void modrm(int mod, int reg, int rm) {
  emit8((mod << 6) | ((reg & 7) << 3) | (rm & 7));
}

static void emit_mov_imm64(int dst, uint64_t imm) {
  rex(true, 0, 0, dst);
  emit8(0xb8 + (dst & 7));
  emit64(imm);
}

static void emit_mov_imm32(int dst, uint32_t imm) {
  rex(false, 0, 0, dst);
  emit8(0xb8 + (dst & 7));
  emit32(imm);
}

static void emit_mov_rr32(int dst, int src) {
  rex(false, src, 0, dst);
  emit8(0x89);
  modrm(3, src, dst);
}

static void emit_load_membase32(int dst, int base, uint32_t disp) {
  rex(false, dst, 0, base);
  emit8(0x8b);
  modrm(2, dst, base);
  if ((base & 7) == HR_RSP) {
    emit8(0x24);
  }
  emit32(disp);
}

static void emit_store_membase32(int base, uint32_t disp, int src) {
  rex(false, src, 0, base);
  emit8(0x89);
  modrm(2, src, base);
  if ((base & 7) == HR_RSP) {
    emit8(0x24);
  }
  emit32(disp);
}

static void emit_load_membase8(int dst, int base, uint32_t disp) {
  rex(false, dst, 0, base);
  emit8(0x0f);
  emit8(0xb6);
  modrm(2, dst, base);
  if ((base & 7) == HR_RSP) {
    emit8(0x24);
  }
  emit32(disp);
}

static void emit_store_membase8(int base, uint32_t disp, int src) {
  rex(false, src, 0, base);
  emit8(0x88);
  modrm(2, src, base);
  if ((base & 7) == HR_RSP) {
    emit8(0x24);
  }
  emit32(disp);
}

static void emit_load_membase16(int dst, int base, uint32_t disp) {
  rex(false, dst, 0, base);
  emit8(0x0f);
  emit8(0xb7);
  modrm(2, dst, base);
  if ((base & 7) == HR_RSP) {
    emit8(0x24);
  }
  emit32(disp);
}

static void emit_store_membase16(int base, uint32_t disp, int src) {
  emit8(0x66);
  rex(false, src, 0, base);
  emit8(0x89);
  modrm(2, src, base);
  if ((base & 7) == HR_RSP) {
    emit8(0x24);
  }
  emit32(disp);
}

static void emit_load_cpu_reg(int dst, int guest_reg, int width) {
  if (width == 1) {
    emit_load_membase8(dst, HR_R12, CPU_OFF_GPR(guest_reg & 3) + ((guest_reg >> 2) & 1));
  }
  else if (width == 2) {
    emit_load_membase16(dst, HR_R12, CPU_OFF_GPR(guest_reg));
  }
  else {
    emit_load_membase32(dst, HR_R12, CPU_OFF_GPR(guest_reg));
  }
}

static void emit_store_cpu_reg(int guest_reg, int src, int width) {
  if (width == 1) {
    emit_store_membase8(HR_R12, CPU_OFF_GPR(guest_reg & 3) + ((guest_reg >> 2) & 1), src);
  }
  else if (width == 2) {
    emit_store_membase16(HR_R12, CPU_OFF_GPR(guest_reg), src);
  }
  else {
    emit_store_membase32(HR_R12, CPU_OFF_GPR(guest_reg), src);
  }
}

static void emit_add_imm32(int dst, int32_t imm) {
  rex(false, 0, 0, dst);
  emit8(0x81);
  modrm(3, 0, dst);
  emit32((uint32_t)imm);
}

static void emit_sub_imm32(int dst, int32_t imm) {
  rex(false, 0, 0, dst);
  emit8(0x81);
  modrm(3, 5, dst);
  emit32((uint32_t)imm);
}

static void emit_and_imm32(int dst, uint32_t imm) {
  rex(false, 0, 0, dst);
  emit8(0x81);
  modrm(3, 4, dst);
  emit32(imm);
}

static void emit_bin_rr32(uint8_t opcode, int dst, int src) {
  rex(false, src, 0, dst);
  emit8(opcode);
  modrm(3, src, dst);
}

static void emit_call_reg(int reg) {
  rex(false, 0, 0, reg);
  emit8(0xff);
  modrm(3, 2, reg);
}

static void emit_push_reg(int reg) {
  rex(false, 0, 0, reg);
  emit8(0x50 + (reg & 7));
}

static void emit_pop_reg(int reg) {
  rex(false, 0, 0, reg);
  emit8(0x58 + (reg & 7));
}

static void emit_ret(void) {
  emit8(0xc3);
}

static void emit_movzx8_rr32(int dst, int src) {
  rex(false, dst, 0, src);
  emit8(0x0f);
  emit8(0xb6);
  modrm(3, dst, src);
}

static void emit_movzx16_rr32(int dst, int src) {
  rex(false, dst, 0, src);
  emit8(0x0f);
  emit8(0xb7);
  modrm(3, dst, src);
}

static void emit_movsx8_rr32(int dst, int src) {
  rex(false, dst, 0, src);
  emit8(0x0f);
  emit8(0xbe);
  modrm(3, dst, src);
}

static void emit_movsx16_rr32(int dst, int src) {
  rex(false, dst, 0, src);
  emit8(0x0f);
  emit8(0xbf);
  modrm(3, dst, src);
}

static void emit_sh_imm8(int dst, uint8_t ext, uint8_t imm) {
  rex(false, 0, 0, dst);
  emit8(0xc1);
  modrm(3, ext, dst);
  emit8(imm);
}

static void emit_sh_cl(int dst, uint8_t ext) {
  rex(false, 0, 0, dst);
  emit8(0xd3);
  modrm(3, ext, dst);
}

static uint32_t fetch_u(vaddr_t *pc, int len) {
  uint32_t val = vaddr_read(*pc, len);
  *pc += len;
  return val;
}

static int32_t fetch_s(vaddr_t *pc, int len) {
  uint32_t val = fetch_u(pc, len);
  if (len == 1) return (int8_t)val;
  if (len == 2) return (int16_t)val;
  return (int32_t)val;
}

static void op_none(JOperand *op) {
  memset(op, 0, sizeof(*op));
  op->type = O_NONE;
  op->base = op->index = -1;
}

static void op_reg(JOperand *op, int reg, int width) {
  op_none(op);
  op->type = O_REG;
  op->reg = reg;
  op->width = width;
}

static void op_imm(JOperand *op, uint32_t imm, int width) {
  op_none(op);
  op->type = O_IMM;
  op->imm = imm;
  op->simm = (int32_t)imm;
  op->width = width;
}

static bool decode_modrm(vaddr_t *pc, JOperand *rm, JOperand *reg, int width, bool need_mem) {
  uint8_t b = fetch_u(pc, 1);
  int mod = b >> 6;
  int r = (b >> 3) & 7;
  int m = b & 7;

  if (reg) {
    op_reg(reg, r, width);
  }

  if (mod == 3) {
    op_reg(rm, m, width);
    return true;
  }

  if (!need_mem) {
    return false;
  }

  op_none(rm);
  rm->type = O_MEM;
  rm->width = width;
  rm->base = -1;
  rm->index = -1;
  rm->scale = 0;
  rm->disp = 0;

  if (m == 4) {
    uint8_t sib = fetch_u(pc, 1);
    rm->scale = sib >> 6;
    int index = (sib >> 3) & 7;
    int base = sib & 7;
    if (index != R_ESP) rm->index = index;
    rm->base = base;
  }
  else {
    rm->base = m;
  }

  if (mod == 0) {
    if (rm->base == R_EBP) {
      rm->base = -1;
      rm->disp = fetch_s(pc, 4);
    }
  }
  else if (mod == 1) {
    rm->disp = fetch_s(pc, 1);
  }
  else if (mod == 2) {
    rm->disp = fetch_s(pc, 4);
  }

  return true;
}

static JInstr *append_insn(JBlock *b, JitOp op, uint32_t pc, uint32_t next_pc) {
  if (b->nr >= JIT_MAX_TB_INSNS) {
    return NULL;
  }
  JInstr *in = &b->insn[b->nr++];
  memset(in, 0, sizeof(*in));
  in->op = op;
  in->pc = pc;
  in->next_pc = next_pc;
  op_none(&in->dst);
  op_none(&in->src);
  return in;
}

static bool is_tb_end_opcode(uint8_t opcode) {
  if (opcode == 0xd6 || opcode == 0xcc || opcode == 0xcd || opcode == 0xcf) {
    return true;
  }
  return false;
}

static bool decode_one(JBlock *b, vaddr_t *pc) {
  uint32_t start = *pc;
  bool operand16 = false;
  uint8_t opcode = fetch_u(pc, 1);

  if (opcode == 0x66) {
    operand16 = true;
    opcode = fetch_u(pc, 1);
  }

  if (is_tb_end_opcode(opcode)) {
    *pc = start;
    return false;
  }

  int width = operand16 ? 2 : 4;
  JOperand rm, reg, imm;
  JInstr *in;

  switch (opcode) {
    case 0x70 ... 0x7f: {
      int32_t off = fetch_s(pc, 1);
      in = append_insn(b, JOP_JCC, start, *pc);
      if (!in) return false;
      op_imm(&in->dst, (opcode & 0xf), 1);
      op_imm(&in->src, *pc + off, 4);
      return true;
    }

    case 0xe8: {
      int32_t off = fetch_s(pc, 4);
      in = append_insn(b, JOP_CALL, start, *pc);
      if (!in) return false;
      op_imm(&in->dst, *pc + off, 4);
      return true;
    }

    case 0xe9: {
      int32_t off = fetch_s(pc, 4);
      in = append_insn(b, JOP_JMP, start, *pc);
      if (!in) return false;
      op_imm(&in->dst, *pc + off, 4);
      return true;
    }

    case 0xeb: {
      int32_t off = fetch_s(pc, 1);
      in = append_insn(b, JOP_JMP, start, *pc);
      if (!in) return false;
      op_imm(&in->dst, *pc + off, 4);
      return true;
    }

    case 0xc3:
      in = append_insn(b, JOP_RET, start, *pc);
      return in != NULL;

    case 0xc9:
      in = append_insn(b, JOP_LEAVE, start, *pc);
      return in != NULL;

    case 0x98:
      in = append_insn(b, JOP_CWTL, start, *pc);
      if (in) op_imm(&in->dst, 0, width);
      return in != NULL;

    case 0x99:
      in = append_insn(b, JOP_CLTD, start, *pc);
      if (in) op_imm(&in->dst, 0, width);
      return in != NULL;

    case 0x90:
      in = append_insn(b, JOP_NOP, start, *pc);
      return in != NULL;

    case 0xb0 ... 0xb7:
      in = append_insn(b, JOP_MOV, start, *pc + 1);
      if (!in) return false;
      op_reg(&in->dst, opcode & 7, 1);
      op_imm(&in->src, fetch_u(pc, 1), 1);
      in->next_pc = *pc;
      return true;

    case 0xb8 ... 0xbf:
      in = append_insn(b, JOP_MOV, start, *pc + width);
      if (!in) return false;
      op_reg(&in->dst, opcode & 7, width);
      op_imm(&in->src, fetch_u(pc, width), width);
      in->next_pc = *pc;
      return true;

    case 0x88:
    case 0x89:
      if (!decode_modrm(pc, &rm, &reg, opcode == 0x88 ? 1 : width, true)) return false;
      in = append_insn(b, JOP_MOV, start, *pc);
      if (!in) return false;
      in->dst = rm;
      in->src = reg;
      return true;

    case 0x8a:
    case 0x8b:
      if (!decode_modrm(pc, &rm, &reg, opcode == 0x8a ? 1 : width, true)) return false;
      in = append_insn(b, JOP_MOV, start, *pc);
      if (!in) return false;
      in->dst = reg;
      in->src = rm;
      return true;

    case 0xc6:
    case 0xc7:
      if (!decode_modrm(pc, &rm, NULL, opcode == 0xc6 ? 1 : width, true)) return false;
      op_imm(&imm, fetch_u(pc, rm.width), rm.width);
      in = append_insn(b, JOP_MOV, start, *pc);
      if (!in) return false;
      in->dst = rm;
      in->src = imm;
      return true;

    case 0xa1:
      in = append_insn(b, JOP_MOV, start, *pc + 4);
      if (!in) return false;
      op_reg(&in->dst, R_EAX, width);
      op_none(&in->src);
      in->src.type = O_MEM;
      in->src.width = width;
      in->src.base = -1;
      in->src.index = -1;
      in->src.disp = fetch_u(pc, 4);
      in->next_pc = *pc;
      return true;

    case 0xa3:
      in = append_insn(b, JOP_MOV, start, *pc + 4);
      if (!in) return false;
      op_none(&in->dst);
      in->dst.type = O_MEM;
      in->dst.width = width;
      in->dst.base = -1;
      in->dst.index = -1;
      in->dst.disp = fetch_u(pc, 4);
      op_reg(&in->src, R_EAX, width);
      in->next_pc = *pc;
      return true;

    case 0x8d:
      if (!decode_modrm(pc, &rm, &reg, width, true)) return false;
      if (rm.type != O_MEM) return false;
      in = append_insn(b, JOP_LEA, start, *pc);
      if (!in) return false;
      in->dst = reg;
      in->src = rm;
      return true;

    case 0x00: case 0x01: case 0x02: case 0x03:
    case 0x20: case 0x21: case 0x22: case 0x23:
    case 0x28: case 0x29: case 0x2a: case 0x2b:
    case 0x30: case 0x31: case 0x32: case 0x33:
    case 0x38: case 0x39: case 0x3a: case 0x3b: {
      int w = (opcode & 1) ? width : 1;
      if (!decode_modrm(pc, &rm, &reg, w, true)) return false;
      uint8_t group = opcode & 0x38;
      JitOp op = group == 0x00 ? JOP_ADD :
                 group == 0x20 ? JOP_AND :
                 group == 0x28 ? JOP_SUB :
                 group == 0x30 ? JOP_XOR : JOP_CMP;
      in = append_insn(b, op, start, *pc);
      if (!in) return false;
      if (opcode & 2) {
        in->dst = reg;
        in->src = rm;
      }
      else {
        in->dst = rm;
        in->src = reg;
      }
      return true;
    }

    case 0x08: case 0x09: case 0x0a: case 0x0b:
    case 0x84: case 0x85: {
      int w = (opcode & 1) ? width : 1;
      if (!decode_modrm(pc, &rm, &reg, w, true)) return false;
      in = append_insn(b, (opcode == 0x84 || opcode == 0x85) ? JOP_TEST : JOP_OR, start, *pc);
      if (!in) return false;
      if (opcode & 2) {
        in->dst = reg;
        in->src = rm;
      }
      else {
        in->dst = rm;
        in->src = reg;
      }
      return true;
    }

    case 0x04: case 0x05:
    case 0x24: case 0x25:
    case 0x2c: case 0x2d:
    case 0x34: case 0x35:
    case 0x3c: case 0x3d:
    case 0xa8: case 0xa9: {
      int w = (opcode & 1) ? width : 1;
      JitOp op = (opcode & 0xfc) == 0x04 ? JOP_ADD :
                 (opcode & 0xfc) == 0x24 ? JOP_AND :
                 (opcode & 0xfc) == 0x2c ? JOP_SUB :
                 (opcode & 0xfc) == 0x34 ? JOP_XOR :
                 (opcode == 0xa8 || opcode == 0xa9) ? JOP_TEST : JOP_CMP;
      op_imm(&imm, fetch_u(pc, w), w);
      in = append_insn(b, op, start, *pc);
      if (!in) return false;
      op_reg(&in->dst, R_EAX, w);
      in->src = imm;
      return true;
    }

    case 0x40 ... 0x47:
      in = append_insn(b, JOP_INC, start, *pc);
      if (!in) return false;
      op_reg(&in->dst, opcode & 7, width);
      return true;

    case 0x48 ... 0x4f:
      in = append_insn(b, JOP_DEC, start, *pc);
      if (!in) return false;
      op_reg(&in->dst, opcode & 7, width);
      return true;

    case 0x50 ... 0x57:
      in = append_insn(b, JOP_PUSH, start, *pc);
      if (!in) return false;
      op_reg(&in->dst, opcode & 7, width);
      return true;

    case 0x58 ... 0x5f:
      in = append_insn(b, JOP_POP, start, *pc);
      if (!in) return false;
      op_reg(&in->dst, opcode & 7, width);
      return true;

    case 0x68:
      in = append_insn(b, JOP_PUSH, start, *pc + width);
      if (!in) return false;
      op_imm(&in->dst, fetch_u(pc, width), width);
      in->next_pc = *pc;
      return true;

    case 0x6a:
      in = append_insn(b, JOP_PUSH, start, *pc + 1);
      if (!in) return false;
      op_imm(&in->dst, (uint32_t)fetch_s(pc, 1), width);
      in->next_pc = *pc;
      return true;

    case 0x80:
    case 0x81:
    case 0x83: {
      int w = opcode == 0x80 ? 1 : width;
      vaddr_t modrm_pc = *pc;
      uint8_t modrm_byte = vaddr_read(modrm_pc, 1);
      int ext = (modrm_byte >> 3) & 7;
      if (ext != 0 && ext != 1 && ext != 4 && ext != 5 && ext != 6 && ext != 7) return false;
      if (!decode_modrm(pc, &rm, NULL, w, true)) return false;
      uint32_t imm_val = (opcode == 0x83) ? (uint32_t)fetch_s(pc, 1) : fetch_u(pc, w);
      if (w == 2) imm_val &= 0xffff;
      op_imm(&imm, imm_val, w);
      JitOp op = ext == 0 ? JOP_ADD :
                 ext == 1 ? JOP_OR :
                 ext == 4 ? JOP_AND :
                 ext == 5 ? JOP_SUB :
                 ext == 6 ? JOP_XOR : JOP_CMP;
      in = append_insn(b, op, start, *pc);
      if (!in) return false;
      in->dst = rm;
      in->src = imm;
      return true;
    }

    case 0xfe:
    case 0xff: {
      int w = opcode == 0xfe ? 1 : width;
      vaddr_t modrm_pc = *pc;
      uint8_t modrm_byte = vaddr_read(modrm_pc, 1);
      int ext = (modrm_byte >> 3) & 7;
      if (ext != 0 && ext != 1 && ext != 2 && ext != 4 && ext != 6) return false;
      if (opcode == 0xfe && ext != 0 && ext != 1) return false;
      if (!decode_modrm(pc, &rm, NULL, w, true)) return false;
      JitOp op = ext == 0 ? JOP_INC :
                 ext == 1 ? JOP_DEC :
                 ext == 2 ? JOP_CALL :
                 ext == 4 ? JOP_JMP : JOP_PUSH;
      in = append_insn(b, op, start, *pc);
      if (!in) return false;
      in->dst = rm;
      return true;
    }

    case 0xc0:
    case 0xc1:
    case 0xd0:
    case 0xd1:
    case 0xd2:
    case 0xd3: {
      int w = (opcode == 0xc0 || opcode == 0xd0 || opcode == 0xd2) ? 1 : width;
      vaddr_t modrm_pc = *pc;
      uint8_t modrm_byte = vaddr_read(modrm_pc, 1);
      int ext = (modrm_byte >> 3) & 7;
      if (ext != 4 && ext != 5 && ext != 7) return false;
      if (!decode_modrm(pc, &rm, NULL, w, true)) return false;
      JitOp op = ext == 4 ? JOP_SHL : ext == 5 ? JOP_SHR : JOP_SAR;
      in = append_insn(b, op, start, *pc);
      if (!in) return false;
      in->dst = rm;
      if (opcode == 0xc0 || opcode == 0xc1) {
        op_imm(&in->src, fetch_u(pc, 1), 1);
        in->next_pc = *pc;
      }
      else if (opcode == 0xd0 || opcode == 0xd1) {
        op_imm(&in->src, 1, 1);
      }
      else {
        op_reg(&in->src, R_CL, 1);
      }
      return true;
    }

    case 0x0f: {
      uint8_t op2 = fetch_u(pc, 1);
      if (op2 >= 0x80 && op2 <= 0x8f) {
        int32_t off = fetch_s(pc, 4);
        in = append_insn(b, JOP_JCC, start, *pc);
        if (!in) return false;
        op_imm(&in->dst, (op2 & 0xf), 1);
        op_imm(&in->src, *pc + off, 4);
        return true;
      }
      if (op2 >= 0x90 && op2 <= 0x9f) {
        if (!decode_modrm(pc, &rm, NULL, 1, true)) return false;
        in = append_insn(b, JOP_SETCC, start, *pc);
        if (!in) return false;
        in->dst = rm;
        op_imm(&in->src, op2 & 0xf, 1);
        return true;
      }
      if (op2 == 0xb6 || op2 == 0xb7 || op2 == 0xbe || op2 == 0xbf) {
        int sw = (op2 == 0xb6 || op2 == 0xbe) ? 1 : 2;
        if (!decode_modrm(pc, &rm, &reg, sw, true)) return false;
        in = append_insn(b, (op2 == 0xbe || op2 == 0xbf) ? JOP_MOVSX : JOP_MOVZX, start, *pc);
        if (!in) return false;
        reg.width = width;
        in->dst = reg;
        in->src = rm;
        return true;
      }
      *pc = start;
      return false;
    }

    default:
      return false;
  }
}

static bool build_block(vaddr_t pc, JBlock *b) {
  memset(b, 0, sizeof(*b));
  b->pc = pc;
  vaddr_t cur = pc;

  while (b->nr < JIT_MAX_TB_INSNS) {
    vaddr_t before = cur;
    if (!decode_one(b, &cur)) {
      if (b->nr == 0) {
        return false;
      }
      cur = before;
      break;
    }
    JitOp last = b->insn[b->nr - 1].op;
    if (last == JOP_JMP || last == JOP_JCC || last == JOP_CALL || last == JOP_RET) {
      break;
    }
  }

  b->end_pc = cur;
  return b->nr > 0;
}

static bool can_emit_insn(const JInstr *in) {
  switch (in->op) {
    case JOP_NOP:
      return true;
    case JOP_LEA:
      return in->dst.type == O_REG && in->src.type == O_MEM && in->dst.width == 4;
    case JOP_PUSH:
      return in->dst.type == O_REG || in->dst.type == O_IMM;
    case JOP_POP:
      return in->dst.type == O_REG;
    case JOP_MOV:
    case JOP_ADD:
    case JOP_SUB:
    case JOP_AND:
    case JOP_OR:
    case JOP_XOR:
    case JOP_CMP:
    case JOP_TEST:
    case JOP_INC:
    case JOP_DEC:
    case JOP_MOVZX:
    case JOP_MOVSX:
    case JOP_SHL:
    case JOP_SHR:
    case JOP_SAR:
    case JOP_JMP:
    case JOP_JCC:
    case JOP_CALL:
    case JOP_RET:
    case JOP_SETCC:
    case JOP_LEAVE:
    case JOP_CWTL:
    case JOP_CLTD:
      return true;
    default:
      return false;
  }
}

static void emit_compute_addr(const JOperand *op, int dst) {
  emit_mov_imm32(dst, (uint32_t)op->disp);
  if (op->base != -1) {
    emit_load_cpu_reg(HR_R10, op->base, 4);
    emit_bin_rr32(0x01, dst, HR_R10);
  }
  if (op->index != -1) {
    emit_load_cpu_reg(HR_R10, op->index, 4);
    if (op->scale != 0) {
      emit_sh_imm8(HR_R10, 4, op->scale);
    }
    emit_bin_rr32(0x01, dst, HR_R10);
  }
}

static void emit_read_op(const JOperand *op, int dst) {
  switch (op->type) {
    case O_REG:
      emit_load_cpu_reg(dst, op->reg, op->width);
      break;
    case O_IMM:
      emit_mov_imm32(dst, op->imm);
      break;
    case O_MEM:
      emit_compute_addr(op, HR_RDI);
      emit_mov_imm32(HR_RSI, op->width);
      emit_mov_imm64(HR_R10, (uint64_t)(uintptr_t)jit_vaddr_read);
      emit_call_reg(HR_R10);
      if (dst != HR_RAX) {
        emit_mov_rr32(dst, HR_RAX);
      }
      break;
    default:
      assert(0);
  }
}

static void emit_write_op(const JOperand *op, int src) {
  switch (op->type) {
    case O_REG:
      emit_store_cpu_reg(op->reg, src, op->width);
      break;
    case O_MEM:
      emit_compute_addr(op, HR_RDI);
      emit_mov_imm32(HR_RSI, op->width);
      if (src != HR_RDX) {
        emit_mov_rr32(HR_RDX, src);
      }
      emit_mov_imm64(HR_R10, (uint64_t)(uintptr_t)jit_vaddr_write);
      emit_call_reg(HR_R10);
      break;
    default:
      assert(0);
  }
}

static void emit_mask_result(int reg, int width) {
  if (width == 1) {
    emit_and_imm32(reg, 0xff);
  }
  else if (width == 2) {
    emit_and_imm32(reg, 0xffff);
  }
}

static void emit_call_flags3(void (*fn)(uint32_t, uint32_t, uint32_t, uint32_t),
    int lhs, int rhs, int res, int width) {
  if (lhs != HR_RDI) emit_mov_rr32(HR_RDI, lhs);
  if (rhs != HR_RSI) emit_mov_rr32(HR_RSI, rhs);
  if (res != HR_RDX) emit_mov_rr32(HR_RDX, res);
  emit_mov_imm32(HR_RCX, width);
  emit_mov_imm64(HR_R10, (uint64_t)(uintptr_t)fn);
  emit_call_reg(HR_R10);
}

static void emit_call_flags2(void (*fn)(uint32_t, uint32_t, uint32_t),
    int lhs, int res, int width) {
  if (lhs != HR_RDI) emit_mov_rr32(HR_RDI, lhs);
  if (res != HR_RSI) emit_mov_rr32(HR_RSI, res);
  emit_mov_imm32(HR_RDX, width);
  emit_mov_imm64(HR_R10, (uint64_t)(uintptr_t)fn);
  emit_call_reg(HR_R10);
}

static void emit_call_logic_flags(int res, int width) {
  if (res != HR_RDI) emit_mov_rr32(HR_RDI, res);
  emit_mov_imm32(HR_RSI, width);
  emit_mov_imm64(HR_R10, (uint64_t)(uintptr_t)jit_set_logic_flags);
  emit_call_reg(HR_R10);
}

static bool emit_insn(const JInstr *in, uint32_t insn_index) {
  if (!can_emit_insn(in)) {
    return false;
  }

  uint32_t sbase = insn_index * 4;
#define S(n) ((sbase + (n)) * 4)

  switch (in->op) {
    case JOP_NOP:
      emit8(0x90);
      return true;

    case JOP_MOV:
      emit_read_op(&in->src, HR_RAX);
      emit_write_op(&in->dst, HR_RAX);
      return true;

    case JOP_LEA:
      emit_compute_addr(&in->src, HR_RAX);
      emit_write_op(&in->dst, HR_RAX);
      return true;

    case JOP_PUSH:
      emit_read_op(&in->dst, HR_RAX);
      emit_load_cpu_reg(HR_R11, R_ESP, 4);
      emit_sub_imm32(HR_R11, in->dst.width == 2 ? 2 : 4);
      emit_store_cpu_reg(R_ESP, HR_R11, 4);
      emit_mov_rr32(HR_RDI, HR_R11);
      emit_mov_imm32(HR_RSI, in->dst.width == 2 ? 2 : 4);
      emit_mov_rr32(HR_RDX, HR_RAX);
      emit_mov_imm64(HR_R10, (uint64_t)(uintptr_t)jit_vaddr_write);
      emit_call_reg(HR_R10);
      return true;

    case JOP_POP:
      emit_load_cpu_reg(HR_RDI, R_ESP, 4);
      emit_mov_imm32(HR_RSI, in->dst.width == 2 ? 2 : 4);
      emit_mov_imm64(HR_R10, (uint64_t)(uintptr_t)jit_vaddr_read);
      emit_call_reg(HR_R10);
      emit_load_cpu_reg(HR_R11, R_ESP, 4);
      emit_add_imm32(HR_R11, in->dst.width == 2 ? 2 : 4);
      emit_store_cpu_reg(R_ESP, HR_R11, 4);
      emit_write_op(&in->dst, HR_RAX);
      return true;

    case JOP_ADD:
      emit_read_op(&in->dst, HR_RAX);
      emit_store_membase32(HR_R14, S(0), HR_RAX);
      emit_read_op(&in->src, HR_RBX);
      emit_store_membase32(HR_R14, S(1), HR_RBX);
      emit_load_membase32(HR_RAX, HR_R14, S(0));
      emit_bin_rr32(0x01, HR_RAX, HR_RBX);
      emit_mask_result(HR_RAX, in->dst.width);
      emit_store_membase32(HR_R14, S(2), HR_RAX);
      emit_write_op(&in->dst, HR_RAX);
      emit_load_membase32(HR_RDI, HR_R14, S(0));
      emit_load_membase32(HR_RSI, HR_R14, S(1));
      emit_load_membase32(HR_RDX, HR_R14, S(2));
      emit_call_flags3(jit_set_add_flags, HR_RDI, HR_RSI, HR_RDX, in->dst.width);
      return true;

    case JOP_SUB:
      emit_read_op(&in->dst, HR_RAX);
      emit_store_membase32(HR_R14, S(0), HR_RAX);
      emit_read_op(&in->src, HR_RBX);
      emit_store_membase32(HR_R14, S(1), HR_RBX);
      emit_load_membase32(HR_RAX, HR_R14, S(0));
      emit_bin_rr32(0x29, HR_RAX, HR_RBX);
      emit_mask_result(HR_RAX, in->dst.width);
      emit_store_membase32(HR_R14, S(2), HR_RAX);
      emit_write_op(&in->dst, HR_RAX);
      emit_load_membase32(HR_RDI, HR_R14, S(0));
      emit_load_membase32(HR_RSI, HR_R14, S(1));
      emit_load_membase32(HR_RDX, HR_R14, S(2));
      emit_call_flags3(jit_set_sub_flags, HR_RDI, HR_RSI, HR_RDX, in->dst.width);
      return true;

    case JOP_CMP:
      emit_read_op(&in->dst, HR_RAX);
      emit_store_membase32(HR_R14, S(0), HR_RAX);
      emit_read_op(&in->src, HR_RBX);
      emit_store_membase32(HR_R14, S(1), HR_RBX);
      emit_load_membase32(HR_RAX, HR_R14, S(0));
      emit_bin_rr32(0x29, HR_RAX, HR_RBX);
      emit_mask_result(HR_RAX, in->dst.width);
      emit_store_membase32(HR_R14, S(2), HR_RAX);
      emit_load_membase32(HR_RDI, HR_R14, S(0));
      emit_load_membase32(HR_RSI, HR_R14, S(1));
      emit_load_membase32(HR_RDX, HR_R14, S(2));
      emit_call_flags3(jit_set_sub_flags, HR_RDI, HR_RSI, HR_RDX, in->dst.width);
      return true;

    case JOP_AND:
    case JOP_OR:
    case JOP_XOR:
    case JOP_TEST: {
      uint8_t op = in->op == JOP_AND || in->op == JOP_TEST ? 0x21 :
                   in->op == JOP_OR ? 0x09 : 0x31;
      emit_read_op(&in->dst, HR_RAX);
      emit_read_op(&in->src, HR_RBX);
      emit_bin_rr32(op, HR_RAX, HR_RBX);
      emit_mask_result(HR_RAX, in->dst.width);
      emit_store_membase32(HR_R14, S(0), HR_RAX);
      if (in->op != JOP_TEST) {
        emit_write_op(&in->dst, HR_RAX);
      }
      emit_load_membase32(HR_RDI, HR_R14, S(0));
      emit_call_logic_flags(HR_RDI, in->dst.width);
      return true;
    }

    case JOP_INC:
      emit_read_op(&in->dst, HR_RAX);
      emit_store_membase32(HR_R14, S(0), HR_RAX);
      emit_add_imm32(HR_RAX, 1);
      emit_mask_result(HR_RAX, in->dst.width);
      emit_store_membase32(HR_R14, S(1), HR_RAX);
      emit_write_op(&in->dst, HR_RAX);
      emit_load_membase32(HR_RDI, HR_R14, S(0));
      emit_load_membase32(HR_RSI, HR_R14, S(1));
      emit_call_flags2(jit_set_inc_flags, HR_RDI, HR_RSI, in->dst.width);
      return true;

    case JOP_DEC:
      emit_read_op(&in->dst, HR_RAX);
      emit_store_membase32(HR_R14, S(0), HR_RAX);
      emit_sub_imm32(HR_RAX, 1);
      emit_mask_result(HR_RAX, in->dst.width);
      emit_store_membase32(HR_R14, S(1), HR_RAX);
      emit_write_op(&in->dst, HR_RAX);
      emit_load_membase32(HR_RDI, HR_R14, S(0));
      emit_load_membase32(HR_RSI, HR_R14, S(1));
      emit_call_flags2(jit_set_dec_flags, HR_RDI, HR_RSI, in->dst.width);
      return true;

    case JOP_MOVZX:
      emit_read_op(&in->src, HR_RAX);
      if (in->src.width == 1) emit_movzx8_rr32(HR_RAX, HR_RAX);
      else emit_movzx16_rr32(HR_RAX, HR_RAX);
      emit_write_op(&in->dst, HR_RAX);
      return true;

    case JOP_MOVSX:
      emit_read_op(&in->src, HR_RAX);
      if (in->src.width == 1) emit_movsx8_rr32(HR_RAX, HR_RAX);
      else emit_movsx16_rr32(HR_RAX, HR_RAX);
      emit_write_op(&in->dst, HR_RAX);
      return true;

    case JOP_SHL:
    case JOP_SHR:
    case JOP_SAR: {
      uint8_t ext = in->op == JOP_SHL ? 4 : in->op == JOP_SHR ? 5 : 7;
      emit_read_op(&in->dst, HR_RAX);
      if (in->src.type == O_IMM) {
        emit_sh_imm8(HR_RAX, ext, in->src.imm & 0xff);
      }
      else {
        emit_read_op(&in->src, HR_RCX);
        emit_sh_cl(HR_RAX, ext);
      }
      emit_mask_result(HR_RAX, in->dst.width);
      emit_store_membase32(HR_R14, S(0), HR_RAX);
      emit_write_op(&in->dst, HR_RAX);
      emit_load_membase32(HR_RDI, HR_R14, S(0));
      emit_mov_imm32(HR_RSI, in->dst.width);
      emit_mov_imm64(HR_R10, (uint64_t)(uintptr_t)jit_set_zfsf_flags);
      emit_call_reg(HR_R10);
      return true;
    }

    case JOP_JMP:
      if (in->dst.type == O_IMM) {
        emit_mov_imm32(HR_RAX, in->dst.imm);
      }
      else {
        emit_read_op(&in->dst, HR_RAX);
      }
      emit_store_membase32(HR_R12, CPU_OFF_EIP, HR_RAX);
      return true;

    case JOP_JCC:
      emit_mov_imm32(HR_RDI, in->dst.imm);
      emit_mov_imm32(HR_RSI, in->src.imm);
      emit_mov_imm32(HR_RDX, in->next_pc);
      emit_mov_imm64(HR_R10, (uint64_t)(uintptr_t)jit_set_jcc_eip);
      emit_call_reg(HR_R10);
      return true;

    case JOP_CALL:
      if (in->dst.type == O_IMM) {
        emit_mov_imm32(HR_RAX, in->dst.imm);
      }
      else {
        emit_read_op(&in->dst, HR_RAX);
      }
      emit_store_membase32(HR_R14, S(0), HR_RAX);
      emit_load_cpu_reg(HR_R11, R_ESP, 4);
      emit_sub_imm32(HR_R11, 4);
      emit_store_cpu_reg(R_ESP, HR_R11, 4);
      emit_mov_rr32(HR_RDI, HR_R11);
      emit_mov_imm32(HR_RSI, 4);
      emit_mov_imm32(HR_RDX, in->next_pc);
      emit_mov_imm64(HR_R10, (uint64_t)(uintptr_t)jit_vaddr_write);
      emit_call_reg(HR_R10);
      emit_load_membase32(HR_RAX, HR_R14, S(0));
      emit_store_membase32(HR_R12, CPU_OFF_EIP, HR_RAX);
      return true;

    case JOP_RET:
      emit_load_cpu_reg(HR_RDI, R_ESP, 4);
      emit_mov_imm32(HR_RSI, 4);
      emit_mov_imm64(HR_R10, (uint64_t)(uintptr_t)jit_vaddr_read);
      emit_call_reg(HR_R10);
      emit_store_membase32(HR_R12, CPU_OFF_EIP, HR_RAX);
      emit_load_cpu_reg(HR_R11, R_ESP, 4);
      emit_add_imm32(HR_R11, 4);
      emit_store_cpu_reg(R_ESP, HR_R11, 4);
      return true;

    case JOP_SETCC:
      emit_mov_imm32(HR_RDI, in->src.imm);
      emit_mov_imm64(HR_R10, (uint64_t)(uintptr_t)jit_eval_cc);
      emit_call_reg(HR_R10);
      emit_write_op(&in->dst, HR_RAX);
      return true;

    case JOP_LEAVE:
      emit_load_cpu_reg(HR_RAX, R_EBP, 4);
      emit_store_cpu_reg(R_ESP, HR_RAX, 4);
      emit_mov_rr32(HR_RDI, HR_RAX);
      emit_mov_imm32(HR_RSI, 4);
      emit_mov_imm64(HR_R10, (uint64_t)(uintptr_t)jit_vaddr_read);
      emit_call_reg(HR_R10);
      emit_store_cpu_reg(R_EBP, HR_RAX, 4);
      emit_load_cpu_reg(HR_R11, R_ESP, 4);
      emit_add_imm32(HR_R11, 4);
      emit_store_cpu_reg(R_ESP, HR_R11, 4);
      return true;

    case JOP_CWTL:
      emit_load_cpu_reg(HR_RAX, R_EAX, in->dst.width ? in->dst.width : 2);
      if (in->dst.width == 2) {
        emit_movsx8_rr32(HR_RAX, HR_RAX);
        emit_store_cpu_reg(R_EAX, HR_RAX, 2);
      }
      else {
        emit_movsx16_rr32(HR_RAX, HR_RAX);
        emit_store_cpu_reg(R_EAX, HR_RAX, 4);
      }
      return true;

    case JOP_CLTD:
      emit_load_cpu_reg(HR_RAX, R_EAX, 4);
      emit_sh_imm8(HR_RAX, 7, 31);
      emit_store_cpu_reg(R_EDX, HR_RAX, 4);
      return true;

    default:
      return false;
  }
#undef S
}

static TB *compile_tb(vaddr_t pc) {
  JBlock b;
  if (!build_block(pc, &b)) {
    return NULL;
  }

  uint8_t *start = codebuf.cur;

  emit_push_reg(HR_RBX);
  emit_push_reg(HR_R12);
  emit_push_reg(HR_R13);
  emit_push_reg(HR_R14);
  emit_push_reg(HR_R15);
  emit_mov_imm64(HR_R12, (uint64_t)(uintptr_t)&cpu);
  emit_mov_imm64(HR_R14, (uint64_t)(uintptr_t)jit_scratch);

  uint32_t emitted = 0;
  for (uint32_t i = 0; i < b.nr; i++) {
    if (!emit_insn(&b.insn[i], i)) {
      codebuf.cur = start;
      return NULL;
    }
    emitted++;
  }

  JitOp last = b.insn[b.nr - 1].op;
  if (last != JOP_JMP && last != JOP_JCC && last != JOP_CALL && last != JOP_RET) {
    emit_mov_imm32(HR_RAX, b.end_pc);
    emit_store_membase32(HR_R12, CPU_OFF_EIP, HR_RAX);
  }

  emit_load_membase32(HR_RDI, HR_R12, CPU_OFF_EIP);
  emit_mov_imm64(HR_R10, (uint64_t)(uintptr_t)jit_handle_irq);
  emit_call_reg(HR_R10);

  emit_mov_imm32(HR_RAX, emitted);
  emit_pop_reg(HR_R15);
  emit_pop_reg(HR_R14);
  emit_pop_reg(HR_R13);
  emit_pop_reg(HR_R12);
  emit_pop_reg(HR_RBX);
  emit_ret();

  TB *tb = &jit_cache[cache_index(pc)];
  tb->pc = pc;
  tb->code = start;
  tb->nr_insn = emitted;
  return tb;
}

void jit_init(void) {
#if JIT_ENABLE
  if (codebuf.start == NULL) {
    size_t size = JIT_CODE_SIZE;
    void *mem = mmap(NULL, size, PROT_READ | PROT_WRITE | PROT_EXEC,
        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    Assert(mem != MAP_FAILED, "can not allocate JIT code cache");
    codebuf.start = mem;
    codebuf.cur = mem;
    codebuf.end = codebuf.start + size;
  }
  memset(jit_cache, 0, sizeof(jit_cache));
#endif
}

uint32_t jit_exec(uint64_t limit) {
#if JIT_ENABLE
  if (limit == 0 || nemu_state != NEMU_RUNNING) {
    return 0;
  }

#ifdef DEBUG
  return 0;
#endif

#ifdef DIFF_TEST
  return 0;
#endif

  TB *tb = &jit_cache[cache_index(cpu.eip)];
  if (tb->code == NULL || tb->pc != cpu.eip) {
    jit_misses++;
    tb = compile_tb(cpu.eip);
    if (tb == NULL) {
      jit_fallbacks++;
      return 0;
    }
  }
  if (limit < tb->nr_insn) {
    return 0;
  }

  jit_hits++;
  uint32_t done = ((tb_func_t)tb->code)(limit);
  return done;
#else
  return 0;
#endif
}
