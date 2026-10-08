/** \file vm-arm.c
 * AArch64 template JIT.
 *
 * The bytecode register file lives below x29. Complex operations call the
 * same C helpers as the interpreter; trivial moves and control flow are
 * emitted directly.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "p2.h"
#include "internal.h"
#include "opcodes.h"
#include "asm.h"
#include "khash.h"
#include "table.h"

#if !defined(__aarch64__) && !defined(_M_ARM64)
#error "core/vm-arm.c requires AArch64"
#endif

#define A64(ins) ASMI((uint32_t)(ins))
#define A64_XZR 31
#define A64_IP0 16
#define A64_IP1 17

static void a64_mov(Potion *P, PNAsm * volatile *asmp, unsigned rd, unsigned rn) {
  A64(0xaa0003e0u | (rn << 16) | rd); /* orr xd, xzr, xn */
}

static void a64_imm(Potion *P, PNAsm * volatile *asmp, unsigned rd, uint64_t v) {
  A64(0xd2800000u | ((v & 0xffffu) << 5) | rd);
  A64(0xf2a00000u | (((v >> 16) & 0xffffu) << 5) | rd);
  A64(0xf2c00000u | (((v >> 32) & 0xffffu) << 5) | rd);
  A64(0xf2e00000u | (((v >> 48) & 0xffffu) << 5) | rd);
}

static void a64_sub_imm(Potion *P, PNAsm * volatile *asmp, unsigned rd,
                        unsigned rn, unsigned imm) {
  if (imm <= 4095) {
    A64(0xd1000000u | (imm << 10) | (rn << 5) | rd);
  } else {
    a64_imm(P, asmp, A64_IP1, imm);
    A64(0xcb000000u | (A64_IP1 << 16) | (rn << 5) | rd);
  }
}

static void a64_load(Potion *P, PNAsm * volatile *asmp, unsigned rt, long reg) {
  long off = -((reg + 1) * (long)sizeof(PN));
  if (off >= -256 && off <= 255) {
    A64(0xf8400000u | (((uint32_t)off & 0x1ffu) << 12) | (29u << 5) | rt);
  } else {
    a64_sub_imm(P, asmp, A64_IP1, 29, (unsigned)-off);
    A64(0xf9400000u | (A64_IP1 << 5) | rt);
  }
}

static void a64_store(Potion *P, PNAsm * volatile *asmp, unsigned rt, long reg) {
  long off = -((reg + 1) * (long)sizeof(PN));
  if (off >= -256 && off <= 255) {
    A64(0xf8000000u | (((uint32_t)off & 0x1ffu) << 12) | (29u << 5) | rt);
  } else {
    a64_sub_imm(P, asmp, A64_IP1, 29, (unsigned)-off);
    A64(0xf9000000u | (A64_IP1 << 5) | rt);
  }
}

static void a64_load_mem(Potion *P, PNAsm * volatile *asmp, unsigned rt,
                         unsigned rn, unsigned off) {
  if ((off & 7) == 0 && off / 8 < 4096) {
    A64(0xf9400000u | ((off / 8) << 10) | (rn << 5) | rt);
  } else {
    a64_imm(P, asmp, A64_IP1, off);
    A64(0xf8606800u | (A64_IP1 << 16) | (rn << 5) | rt); /* ldr [xn,x17] */
  }
}

static void a64_store_mem(Potion *P, PNAsm * volatile *asmp, unsigned rt,
                          unsigned rn, unsigned off) {
  if ((off & 7) == 0 && off / 8 < 4096) {
    A64(0xf9000000u | ((off / 8) << 10) | (rn << 5) | rt);
  } else {
    a64_imm(P, asmp, A64_IP1, off);
    A64(0xf8206800u | (A64_IP1 << 16) | (rn << 5) | rt); /* str [xn,x17] */
  }
}

static void a64_arg_reg(Potion *P, PNAsm * volatile *asmp, long reg, int arg) {
  if (arg < 8) {
    a64_load(P, asmp, (unsigned)arg, reg);
  } else {
    a64_load(P, asmp, A64_IP0, reg);
    a64_store_mem(P, asmp, A64_IP0, 31, (unsigned)(arg - 8) * 8);
  }
}

static void a64_arg_imm(Potion *P, PNAsm * volatile *asmp, PN value, int arg) {
  if (arg < 8) {
    a64_imm(P, asmp, (unsigned)arg, (uint64_t)value);
  } else {
    a64_imm(P, asmp, A64_IP0, (uint64_t)value);
    a64_store_mem(P, asmp, A64_IP0, 31, (unsigned)(arg - 8) * 8);
  }
}

static void a64_call(Potion *P, PNAsm * volatile *asmp, const void *fn) {
  a64_imm(P, asmp, A64_IP0, (uint64_t)(uintptr_t)fn);
  A64(0xd63f0000u | (A64_IP0 << 5)); /* blr x16 */
}

static void a64_result(Potion *P, PNAsm * volatile *asmp, long reg) {
  a64_store(P, asmp, 0, reg);
}

static PN potion_arm_local_get(PN value) {
  return PN_IS_REF(value) ? PN_DEREF(value) : value;
}

static PN potion_arm_local_set(Potion *P, PN slot, PN value) {
  if (PN_IS_REF(slot)) {
    PN_DEREF(slot) = value;
    PN_TOUCH(slot);
    return slot;
  }
  return value;
}

static PN potion_arm_tuple_get(PN value, PN key, int indirect) {
  vPN(Tuple) tuple = (vPN(Tuple))potion_fwd(value);
  long index = indirect ? PN_INT(key) : (long)key;
  return PN_TUPLE_AT(tuple, index);
}

static PN potion_arm_newlick_value(Potion *P, PN value, PN attr, PN inner) {
  return potion_lick(P, value, attr, inner);
}

static PN potion_arm_numcmp(Potion *P, PN a, PN b, int kind) {
  double av, bv;
  if (PN_IS_INT(a) && PN_IS_INT(b)) {
    long ai = PN_INT(a), bi = PN_INT(b);
    switch (kind) {
      case 0: return PN_BOOL(ai < bi);
      case 1: return PN_BOOL(ai <= bi);
      case 2: return PN_BOOL(ai > bi);
      default:return PN_BOOL(ai >= bi);
    }
  }
  av = PN_DBL(a);
  bv = PN_DBL(b);
  switch (kind) {
    case 0: return PN_BOOL(av < bv);
    case 1: return PN_BOOL(av <= bv);
    case 2: return PN_BOOL(av > bv);
    default:return PN_BOOL(av >= bv);
  }
}

static uintptr_t potion_arm_truth(PN value) {
  return PN_TEST1(value) ? 1u : 0u;
}

static PN potion_arm_test_value(PN value) {
  return PN_BOOL(PN_TEST(value));
}

static PN potion_arm_not_value(PN value) {
#ifdef P2
  return PN_ZERO == value ? PN_TRUE : PN_BOOL(!PN_TEST(value));
#else
  return PN_BOOL(!PN_TEST(value));
#endif
}

static PN potion_arm_invoke(Potion *P, PN callable, int argc, PN *args) {
  vPN(Closure) closure;
  int supplied, i;
  if (PN_TYPE(callable) == PN_TVTABLE) {
    args[0] = potion_object_new(P, PN_NIL, callable);
    callable = ((struct PNVtable *)callable)->ctor;
  } else if (PN_TYPE(callable) != PN_TCLOSURE) {
    args[0] = callable;
    callable = potion_obj_get_call(P, callable);
  }
  if (!PN_IS_CLOSURE(callable))
    return PN_NIL;
  closure = PN_CLOSURE(callable);
  supplied = argc - 1;
  if (PN_IS_TUPLE(closure->sig)) {
    for (i = supplied; i < closure->arity; i++) {
      PN sig = potion_sig_at(P, closure->sig, i);
      if (sig)
        args[i + 1] = PN_TUPLE_LEN(sig) == 3
          ? PN_TUPLE_AT(sig, 2)
          : potion_type_default(PN_INT(PN_TUPLE_AT(sig, 1)));
    }
    if (closure->arity > supplied)
      argc = closure->arity + 1;
  }
  return potion_call(P, callable, argc, args);
}

static void potion_arm_named_arg(Potion *P, PN closure, PN name, PN value,
                                 PN *r0, int base) {
  int index = potion_sig_find(P, closure, name);
  if (index < 0)
    potion_fatal("named parameter not found in signature");
  r0[-(base + index + 2)] = value;
}

void potion_arm_setup(Potion *P, struct PNProto * volatile f, PNAsm * volatile *asmp) {
  A64(0xa9bf7bfdu); /* stp x29, x30, [sp,#-16]! */
  A64(0x910003fdu); /* mov x29, sp */
}

void potion_arm_stack(Potion *P, struct PNProto * volatile f, PNAsm * volatile *asmp, long need) {
  unsigned frame = (unsigned)((need + 15) & ~15L);
  a64_sub_imm(P, asmp, 31, 31, frame);
}

void potion_arm_registers(Potion *P, struct PNProto * volatile f, PNAsm * volatile *asmp, long start) {
  PN_HAS_UPVALS(up);
  a64_store(P, asmp, 0, start - 3);
  a64_store(P, asmp, 1, start - 2);
  a64_store(P, asmp, 2, start - 1);
  a64_store(P, asmp, 2, 0);
  if (up) {
    long i, regs = PN_INT(f->stack);
    for (i = 0; i < (long)PN_TUPLE_LEN(f->locals); i++) {
      a64_imm(P, asmp, A64_IP0, PN_NIL);
      a64_store(P, asmp, A64_IP0, regs + i);
    }
  }
}

void potion_arm_local(Potion *P, struct PNProto * volatile f, PNAsm * volatile *asmp, long reg, long arg) {
  unsigned n = (unsigned)(3 + arg);
  if (n < 8) {
    a64_store(P, asmp, n, reg);
  } else {
    a64_load_mem(P, asmp, A64_IP0, 29, 16 + (n - 8) * 8);
    a64_store(P, asmp, A64_IP0, reg);
  }
}

void potion_arm_upvals(Potion *P, struct PNProto * volatile f, PNAsm * volatile *asmp,
                       long lregs, long start, int upc) {
  int i;
  for (i = 0; i < upc; i++) {
    a64_load(P, asmp, A64_IP0, start - 2);
    a64_load_mem(P, asmp, A64_IP0, A64_IP0,
                 sizeof(struct PNClosure) + (unsigned)(i + 1) * sizeof(PN));
    a64_store(P, asmp, A64_IP0, lregs + i);
  }
}

void potion_arm_jmpedit(Potion *P, struct PNProto * volatile f, PNAsm * volatile *asmp,
                        unsigned char *asmj, int dist) {
  uint32_t ins;
  int delta = dist + 4;
  memcpy(&ins, asmj, sizeof(ins));
  if ((ins & 0x7c000000u) == 0x14000000u) {
    ins = (ins & 0xfc000000u) | (((uint32_t)(delta >> 2)) & 0x03ffffffu);
  } else if ((ins & 0x7f000000u) == 0x34000000u) {
    ins = (ins & 0xff00001fu) | ((((uint32_t)(delta >> 2)) & 0x7ffffu) << 5);
  } else {
    ins = (ins & 0xff00001fu) | ((((uint32_t)(delta >> 2)) & 0x7ffffu) << 5);
  }
  memcpy(asmj, &ins, sizeof(ins));
}

void potion_arm_move(Potion *P, struct PNProto * volatile f, PNAsm * volatile *asmp, PN_SIZE pos) {
  PN_OP op = PN_OP_AT(f->asmb, pos);
  a64_load(P, asmp, A64_IP0, op.b);
  a64_store(P, asmp, A64_IP0, op.a);
}

void potion_arm_loadpn(Potion *P, struct PNProto * volatile f, PNAsm * volatile *asmp, PN_SIZE pos) {
  PN_OP op = PN_OP_AT(f->asmb, pos);
  a64_imm(P, asmp, A64_IP0, op.b);
  a64_store(P, asmp, A64_IP0, op.a);
}

PN potion_f_values(Potion *P, PN cl) {
  return potion_fwd(PN_PROTO(PN_CLOSURE(cl)->data[0])->values);
}

PN potion_f_protos(Potion *P, PN cl, PN i) {
  PN protos = PN_PROTO(PN_CLOSURE(cl)->data[0])->protos;
  PN proto = PN_TUPLE_AT(protos, i);
  vPN(Closure) closure = (struct PNClosure *)potion_closure_new(
    P, PN_PROTO(proto)->jit, PN_PROTO(proto)->sig,
    PN_TUPLE_LEN(PN_PROTO(proto)->upvals) + 1);
  closure->data[0] = proto;
  return (PN)closure;
}

void potion_arm_loadk(Potion *P, struct PNProto * volatile f, PNAsm * volatile *asmp, PN_SIZE pos, long start) {
  PN_OP op = PN_OP_AT(f->asmb, pos);
  a64_arg_reg(P, asmp, start - 3, 0);
  a64_arg_reg(P, asmp, start - 2, 1);
  a64_call(P, asmp, potion_f_values);
  a64_load_mem(P, asmp, 0, 0, sizeof(struct PNTuple) + op.b * sizeof(PN));
  a64_result(P, asmp, op.a);
}

void potion_arm_self(Potion *P, struct PNProto * volatile f, PNAsm * volatile *asmp, PN_SIZE pos, long start) {
  PN_OP op = PN_OP_AT(f->asmb, pos);
  a64_load(P, asmp, A64_IP0, start - 1);
  a64_store(P, asmp, A64_IP0, op.a);
}

void potion_arm_getlocal(Potion *P, struct PNProto * volatile f, PNAsm * volatile *asmp, PN_SIZE pos, long regs) {
  PN_OP op = PN_OP_AT(f->asmb, pos);
  a64_arg_reg(P, asmp, regs + op.b, 0);
  a64_call(P, asmp, potion_arm_local_get);
  a64_result(P, asmp, op.a);
}

void potion_arm_setlocal(Potion *P, struct PNProto * volatile f, PNAsm * volatile *asmp, PN_SIZE pos, long regs) {
  PN_OP op = PN_OP_AT(f->asmb, pos);
  PN_HAS_UPVALS(up);
  if (up) {
    a64_arg_reg(P, asmp, regs + op.b, 1);
    a64_arg_reg(P, asmp, op.a, 2);
    a64_arg_imm(P, asmp, (PN)P, 0);
    a64_call(P, asmp, potion_arm_local_set);
    a64_result(P, asmp, regs + op.b);
  } else {
    a64_load(P, asmp, A64_IP0, op.a);
    a64_store(P, asmp, A64_IP0, regs + op.b);
  }
}

void potion_arm_getupval(Potion *P, struct PNProto * volatile f, PNAsm * volatile *asmp, PN_SIZE pos, long lregs) {
  PN_OP op = PN_OP_AT(f->asmb, pos);
  a64_load(P, asmp, A64_IP0, lregs + op.b);
  a64_load_mem(P, asmp, A64_IP0, A64_IP0, sizeof(struct PNObject));
  a64_store(P, asmp, A64_IP0, op.a);
}

void potion_arm_setupval(Potion *P, struct PNProto * volatile f, PNAsm * volatile *asmp, PN_SIZE pos, long lregs) {
  PN_OP op = PN_OP_AT(f->asmb, pos);
  a64_load(P, asmp, A64_IP0, lregs + op.b);
  a64_load(P, asmp, A64_IP1, op.a);
  a64_store_mem(P, asmp, A64_IP1, A64_IP0, sizeof(struct PNObject));
}

#define ARM_CALL3(fn, dst, r1, r2) do { \
  a64_arg_reg(P, asmp, start - 3, 0); \
  a64_arg_reg(P, asmp, (r1), 1); \
  a64_arg_reg(P, asmp, (r2), 2); \
  a64_call(P, asmp, (fn)); \
  a64_result(P, asmp, (dst)); \
} while (0)

void potion_arm_global(Potion *P, struct PNProto * volatile f, PNAsm * volatile *asmp, PN_SIZE pos, long start) {
  PN_OP op = PN_OP_AT(f->asmb, pos);
  ARM_CALL3(potion_define_global, op.a, op.a, op.b);
  a64_load(P, asmp, A64_IP0, op.b);
  a64_store(P, asmp, A64_IP0, op.a);
}

void potion_arm_newtuple(Potion *P, struct PNProto * volatile f, PNAsm * volatile *asmp, PN_SIZE pos, long start) {
  PN_OP op = PN_OP_AT(f->asmb, pos);
  a64_arg_reg(P, asmp, start - 3, 0);
  a64_call(P, asmp, potion_tuple_empty);
  a64_result(P, asmp, op.a);
}

void potion_arm_gettuple(Potion *P, struct PNProto * volatile f, PNAsm * volatile *asmp, PN_SIZE pos, long start) {
  PN_OP op = PN_OP_AT(f->asmb, pos);
  a64_arg_reg(P, asmp, op.a, 0);
  if (op.b & ASM_TPL_IMM) {
    a64_arg_reg(P, asmp, op.b - ASM_TPL_IMM, 1);
    a64_arg_imm(P, asmp, 1, 2);
  } else {
    a64_arg_imm(P, asmp, op.b, 1);
    a64_arg_imm(P, asmp, 0, 2);
  }
  a64_call(P, asmp, potion_arm_tuple_get);
  a64_result(P, asmp, op.a);
}

void potion_arm_settuple(Potion *P, struct PNProto * volatile f, PNAsm * volatile *asmp, PN_SIZE pos, long start) {
  PN_OP op = PN_OP_AT(f->asmb, pos);
  ARM_CALL3(potion_tuple_push, op.a, op.a, op.b);
}

void potion_arm_gettable(Potion *P, struct PNProto * volatile f, PNAsm * volatile *asmp, PN_SIZE pos, long start) {
  PN_OP op = PN_OP_AT(f->asmb, pos);
  a64_arg_reg(P, asmp, start - 3, 0);
  a64_arg_imm(P, asmp, PN_NIL, 1);
  a64_arg_reg(P, asmp, op.a, 2);
  a64_arg_reg(P, asmp, op.b, 3);
  a64_call(P, asmp, potion_table_at);
  a64_result(P, asmp, op.a);
}

void potion_arm_settable(Potion *P, struct PNProto * volatile f, PNAsm * volatile *asmp, PN_SIZE pos, long start) {
  PN_OP op = PN_OP_AT(f->asmb, pos);
  a64_arg_reg(P, asmp, start - 3, 0);
  a64_arg_reg(P, asmp, op.a, 1);
  a64_arg_reg(P, asmp, op.a + 1, 2);
  a64_arg_reg(P, asmp, op.b, 3);
  a64_call(P, asmp, potion_table_set);
  a64_result(P, asmp, op.a);
}

void potion_arm_newlick(Potion *P, struct PNProto * volatile f, PNAsm * volatile *asmp, PN_SIZE pos, long start) {
  PN_OP op = PN_OP_AT(f->asmb, pos);
  a64_arg_reg(P, asmp, start - 3, 0);
  a64_arg_reg(P, asmp, op.a, 1);
  op.b > op.a ? a64_arg_reg(P, asmp, op.a + 1, 2) : a64_arg_imm(P, asmp, PN_NIL, 2);
  op.b > op.a + 1 ? a64_arg_reg(P, asmp, op.b, 3) : a64_arg_imm(P, asmp, PN_NIL, 3);
  a64_call(P, asmp, potion_arm_newlick_value);
  a64_result(P, asmp, op.a);
}

void potion_arm_getpath(Potion *P, struct PNProto * volatile f, PNAsm * volatile *asmp, PN_SIZE pos, long start) {
  PN_OP op = PN_OP_AT(f->asmb, pos);
  a64_arg_reg(P, asmp, start - 3, 0);
  a64_arg_imm(P, asmp, PN_NIL, 1);
  a64_arg_reg(P, asmp, op.a, 2);
  a64_arg_reg(P, asmp, op.b, 3);
  a64_call(P, asmp, potion_obj_get);
  a64_result(P, asmp, op.a);
}

void potion_arm_setpath(Potion *P, struct PNProto * volatile f, PNAsm * volatile *asmp, PN_SIZE pos, long start) {
  PN_OP op = PN_OP_AT(f->asmb, pos);
  a64_arg_reg(P, asmp, start - 3, 0);
  a64_arg_imm(P, asmp, PN_NIL, 1);
  a64_arg_reg(P, asmp, op.a, 2);
  a64_arg_reg(P, asmp, op.a + 1, 3);
  a64_arg_reg(P, asmp, op.b, 4);
  a64_call(P, asmp, potion_obj_set);
}

#define ARM_BINARY(name, fn) \
void potion_arm_##name(Potion *P, struct PNProto * volatile f, PNAsm * volatile *asmp, PN_SIZE pos, long start) { \
  PN_OP op = PN_OP_AT(f->asmb, pos); ARM_CALL3((fn), op.a, op.a, op.b); \
}
ARM_BINARY(add, potion_obj_add)
ARM_BINARY(sub, potion_obj_sub)
ARM_BINARY(mult, potion_obj_mult)
ARM_BINARY(div, potion_obj_div)
ARM_BINARY(rem, potion_obj_rem)
ARM_BINARY(bitl, potion_obj_bitl)
ARM_BINARY(bitr, potion_obj_bitr)

void potion_arm_pow(Potion *P, struct PNProto * volatile f, PNAsm * volatile *asmp, PN_SIZE pos, long start) {
  PN_OP op = PN_OP_AT(f->asmb, pos);
  a64_arg_reg(P, asmp, start - 3, 0);
  a64_arg_imm(P, asmp, PN_NIL, 1);
  a64_arg_reg(P, asmp, op.a, 2);
  a64_arg_reg(P, asmp, op.b, 3);
  a64_call(P, asmp, potion_num_pow);
  a64_result(P, asmp, op.a);
}

ARM_BINARY(neq, potion_vm_neq)
ARM_BINARY(eq, potion_vm_eq)
ARM_BINARY(cmp, potion_vm_cmp)

static void potion_arm_rel(Potion *P, struct PNProto * volatile f, PNAsm * volatile *asmp,
                           PN_SIZE pos, int kind) {
  PN_OP op = PN_OP_AT(f->asmb, pos);
  a64_arg_imm(P, asmp, (PN)P, 0);
  a64_arg_reg(P, asmp, op.a, 1);
  a64_arg_reg(P, asmp, op.b, 2);
  a64_arg_imm(P, asmp, kind, 3);
  a64_call(P, asmp, potion_arm_numcmp);
  a64_result(P, asmp, op.a);
}
void potion_arm_lt(Potion *P, struct PNProto * volatile f, PNAsm * volatile *asmp, PN_SIZE pos) { potion_arm_rel(P,f,asmp,pos,0); }
void potion_arm_lte(Potion *P, struct PNProto * volatile f, PNAsm * volatile *asmp, PN_SIZE pos) { potion_arm_rel(P,f,asmp,pos,1); }
void potion_arm_gt(Potion *P, struct PNProto * volatile f, PNAsm * volatile *asmp, PN_SIZE pos) { potion_arm_rel(P,f,asmp,pos,2); }
void potion_arm_gte(Potion *P, struct PNProto * volatile f, PNAsm * volatile *asmp, PN_SIZE pos) { potion_arm_rel(P,f,asmp,pos,3); }

void potion_arm_bitn(Potion *P, struct PNProto * volatile f, PNAsm * volatile *asmp, PN_SIZE pos, long start) {
  PN_OP op = PN_OP_AT(f->asmb, pos);
  a64_arg_reg(P, asmp, start - 3, 0);
  a64_arg_reg(P, asmp, op.b, 1);
  a64_call(P, asmp, potion_obj_bitn);
  a64_result(P, asmp, op.a);
}

void potion_arm_def(Potion *P, struct PNProto * volatile f, PNAsm * volatile *asmp, PN_SIZE pos, long start) {
  PN_OP op = PN_OP_AT(f->asmb, pos);
  a64_arg_reg(P, asmp, start - 3, 0);
  a64_arg_imm(P, asmp, PN_NIL, 1);
  a64_arg_reg(P, asmp, op.a, 2);
  a64_arg_reg(P, asmp, op.a + 1, 3);
  a64_arg_reg(P, asmp, op.b, 4);
  a64_call(P, asmp, potion_def_method);
  a64_result(P, asmp, op.a);
}

void potion_arm_bind(Potion *P, struct PNProto * volatile f, PNAsm * volatile *asmp, PN_SIZE pos, long start) {
  PN_OP op = PN_OP_AT(f->asmb, pos);
  ARM_CALL3(potion_bind, op.a, op.b, op.a);
}

void potion_arm_message(Potion *P, struct PNProto * volatile f, PNAsm * volatile *asmp, PN_SIZE pos, long start) {
  PN_OP op = PN_OP_AT(f->asmb, pos);
  ARM_CALL3(potion_message, op.a, op.b, op.a);
}

static void a64_jump(Potion *P, PNAsm * volatile *asmp, PN_SIZE pos, long target,
                     PNJumps *jmps, size_t *offs, int *jmpc, uint32_t ins) {
  if (target >= (long)pos) {
    jmps[*jmpc].from = (*asmp)->len;
    A64(ins);
    jmps[*jmpc].to = (PN_SIZE)target + 1;
    (*jmpc)++;
  } else {
    int delta = (int)offs[target + 1] - (int)(*asmp)->len;
    if ((ins & 0x7c000000u) == 0x14000000u)
      ins |= ((uint32_t)(delta >> 2)) & 0x03ffffffu;
    else
      ins |= (((uint32_t)(delta >> 2)) & 0x7ffffu) << 5;
    A64(ins);
  }
}

void potion_arm_jmp(Potion *P, struct PNProto * volatile f, PNAsm * volatile *asmp, PN_SIZE pos,
                    PNJumps *jmps, size_t *offs, int *jmpc) {
  PN_OP op = PN_OP_AT(f->asmb, pos);
  a64_jump(P, asmp, pos, (long)pos + op.a, jmps, offs, jmpc, 0x14000000u);
}

void potion_arm_test(Potion *P, struct PNProto * volatile f, PNAsm * volatile *asmp, PN_SIZE pos) {
  PN_OP op = PN_OP_AT(f->asmb, pos);
  a64_arg_reg(P, asmp, op.a, 0);
  a64_call(P, asmp, potion_arm_test_value);
  a64_result(P, asmp, op.a);
}

void potion_arm_not(Potion *P, struct PNProto * volatile f, PNAsm * volatile *asmp, PN_SIZE pos) {
  PN_OP op = PN_OP_AT(f->asmb, pos);
  a64_arg_reg(P, asmp, op.a, 0);
  a64_call(P, asmp, potion_arm_not_value);
  a64_result(P, asmp, op.a);
}

static void potion_arm_condjmp(Potion *P, struct PNProto * volatile f, PNAsm * volatile *asmp,
                               PN_SIZE pos, PNJumps *jmps, size_t *offs, int *jmpc, int truth) {
  PN_OP op = PN_OP_AT(f->asmb, pos);
  a64_arg_reg(P, asmp, op.a, 0);
  a64_call(P, asmp, potion_arm_truth);
  a64_jump(P, asmp, pos, (long)pos + op.b, jmps, offs, jmpc,
           truth ? 0xb5000000u : 0xb4000000u); /* cbnz/cbz x0 */
}
void potion_arm_testjmp(Potion *P, struct PNProto * volatile f, PNAsm * volatile *asmp, PN_SIZE pos, PNJumps *jmps, size_t *offs, int *jmpc) { potion_arm_condjmp(P,f,asmp,pos,jmps,offs,jmpc,1); }
void potion_arm_notjmp(Potion *P, struct PNProto * volatile f, PNAsm * volatile *asmp, PN_SIZE pos, PNJumps *jmps, size_t *offs, int *jmpc) { potion_arm_condjmp(P,f,asmp,pos,jmps,offs,jmpc,0); }

void potion_arm_named(Potion *P, struct PNProto * volatile f, PNAsm * volatile *asmp, PN_SIZE pos, long start) {
  PN_OP op = PN_OP_AT(f->asmb, pos);
  a64_arg_reg(P, asmp, start - 3, 0);
  a64_arg_reg(P, asmp, op.a, 1);
  a64_arg_reg(P, asmp, op.b - 1, 2);
  a64_arg_reg(P, asmp, op.b, 3);
  a64_sub_imm(P, asmp, 4, 29, sizeof(PN));
  a64_arg_imm(P, asmp, op.a, 5);
  a64_call(P, asmp, potion_arm_named_arg);
}

void potion_arm_call(Potion *P, struct PNProto * volatile f, PNAsm * volatile *asmp, PN_SIZE pos, long start) {
  PN_OP op = PN_OP_AT(f->asmb, pos);
  int argc = op.b - op.a;
  int i;
  for (i = 0; i < argc; i++) {
    a64_load(P, asmp, A64_IP0, op.a + 1 + i);
    a64_store_mem(P, asmp, A64_IP0, 31, (unsigned)i * sizeof(PN));
  }
  a64_arg_reg(P, asmp, start - 3, 0);
  a64_arg_reg(P, asmp, op.a, 1);
  a64_arg_imm(P, asmp, argc, 2);
  A64(0x910003e3u); /* mov x3, sp */
  a64_call(P, asmp, potion_arm_invoke);
  a64_result(P, asmp, op.a);
}

void potion_arm_callset(Potion *P, struct PNProto * volatile f, PNAsm * volatile *asmp, PN_SIZE pos, long start) {
  PN_OP op = PN_OP_AT(f->asmb, pos);
  a64_arg_reg(P, asmp, start - 3, 0);
  a64_arg_reg(P, asmp, op.b, 1);
  a64_call(P, asmp, potion_obj_get_callset);
  a64_result(P, asmp, op.a);
}

void potion_arm_tailcall(Potion *P, struct PNProto * volatile f, PNAsm * volatile *asmp, PN_SIZE pos) {
  potion_fatal("OP_TAILCALL not implemented");
}

void potion_arm_return(Potion *P, struct PNProto * volatile f, PNAsm * volatile *asmp, PN_SIZE pos) {
  PN_OP op = PN_OP_AT(f->asmb, pos);
  a64_load(P, asmp, 0, op.a);
  A64(0x910003bfu); /* mov sp, x29 */
  A64(0xa8c17bfdu); /* ldp x29, x30, [sp],#16 */
  A64(0xd65f03c0u); /* ret */
}

void potion_arm_method(Potion *P, struct PNProto * volatile f, PNAsm * volatile *asmp,
                       PN_SIZE *pos, long lregs, long start, long regs) {
  PN_OP op = PN_OP_AT(f->asmb, *pos);
  PN proto = PN_TUPLE_AT(f->protos, op.b);
  a64_arg_reg(P, asmp, start - 3, 0);
  a64_arg_reg(P, asmp, start - 2, 1);
  a64_arg_imm(P, asmp, op.b, 2);
  a64_call(P, asmp, potion_f_protos);
  a64_result(P, asmp, op.a);
  PN_TUPLE_COUNT(PN_PROTO(proto)->upvals, i, {
    PN_OP capture;
    (*pos)++;
    capture = PN_OP_AT(f->asmb, *pos);
    if (capture.code == OP_GETUPVAL) {
      a64_load(P, asmp, A64_IP1, lregs + capture.b);
    } else if (capture.code == OP_GETLOCAL) {
      a64_arg_reg(P, asmp, start - 3, 0);
      a64_arg_reg(P, asmp, regs + capture.b, 1);
      a64_call(P, asmp, potion_ref);
      a64_store(P, asmp, 0, regs + capture.b);
      a64_mov(P, asmp, A64_IP1, 0);
    } else {
      potion_fatal("missing an upvalue for proto");
    }
    a64_load(P, asmp, A64_IP0, op.a);
    a64_store_mem(P, asmp, A64_IP1, A64_IP0,
                  sizeof(struct PNClosure) + (unsigned)(i + 1) * sizeof(PN));
  });
}

void potion_arm_class(Potion *P, struct PNProto * volatile f, PNAsm * volatile *asmp, PN_SIZE pos, long start) {
  PN_OP op = PN_OP_AT(f->asmb, pos);
  a64_arg_reg(P, asmp, start - 3, 0);
  a64_arg_reg(P, asmp, op.b, 1);
  a64_arg_reg(P, asmp, op.a, 2);
  a64_call(P, asmp, potion_vm_class);
  a64_result(P, asmp, op.a);
}

void potion_arm_finish(Potion *P, struct PNProto * volatile f, PNAsm * volatile *asmp) {
}

static void a64_cmp_return(Potion *P, PNAsm * volatile *asmp, PN key, PN value) {
  a64_imm(P, asmp, A64_IP0, key);
  A64(0xeb10001fu); /* cmp x0, x16 */
  A64(0x540000c1u); /* b.ne +24, over movz/movk/ret */
  a64_imm(P, asmp, 0, value);
  A64(0xd65f03c0u);
}

void potion_arm_mcache(Potion *P, vPN(Vtable) vt, PNAsm * volatile *asmp) {
  unsigned k;
  for (k = kh_end(vt->methods); k > kh_begin(vt->methods); k--) {
    if (kh_exist(PN, vt->methods, k - 1))
      a64_cmp_return(P, asmp, PN_UNIQ(kh_key(PN, vt->methods, k - 1)),
                     kh_val(PN, vt->methods, k - 1));
  }
  a64_imm(P, asmp, 0, PN_NIL);
  A64(0xd65f03c0u);
}

void potion_arm_ivars(Potion *P, PN ivars, PNAsm * volatile *asmp) {
  PN_TUPLE_EACH(ivars, i, value, {
    a64_cmp_return(P, asmp, PN_UNIQ(value), i);
  });
  a64_imm(P, asmp, 0, (PN)-1);
  A64(0xd65f03c0u);
}

MAKE_TARGET(arm);
