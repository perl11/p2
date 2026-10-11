/**\file lib/p5/p5number.c
  p5 number semantics on top of the core PNNumber (Potion keeps C integer
  division/remainder and integer-only pow): exact integer quotients stay
  integers, else a double (7/2 = 3.5); % has the sign of the right operand;
  ** gives a double for negative or huge exponents; doubles print with %.15g.
  The p2 VM and JIT always dispatch / % ** to these methods.

  (c) 2026 perl11 org */
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include "p5.h"

/* 'use integer': a global flag (no lexical scope), set by p5integer(1) / p5integer(0) */
static int p5_use_integer;

static PN p5_integer_pragma(Potion *P, PN cl, PN self, PN flag) {
  p5_use_integer = PN_IS_INT(flag) ? PN_INT(flag) != 0 : PN_TEST(flag);
  return PN_NIL;
}

/* operand as a double: undef/false are 0, true 1, strings are parsed like perl */
static double p5_nv(PN n) {
  if (PN_IS_INT(n)) return (double)PN_INT(n);
  if (n == PN_NIL || n == PN_FALSE) return 0.0;
  if (n == PN_TRUE) return 1.0;
  if (PN_IS_PTR(n)) {
    if (PN_IS_STR(n)) return strtod(PN_STR_PTR(n), NULL);
    if (PN_IS_NUM(n)) return PN_DBL(n);
  }
  return 0.0;
}

/* operand as an integer: doubles truncate (use integer) */
static long p5_iv(PN n) {
  return PN_IS_INT(n) ? PN_INT(n) : (long)p5_nv(n);
}

static PN p5_die(Potion *P, const char *msg) {
  potion_send(P->lobby, PN_STR("die"), PN_STR(msg));
  return PN_NIL;
}

static PN p5_div(Potion *P, PN cl, PN self, PN num) {
  if (p5_use_integer) {
    long a = p5_iv(self), b = p5_iv(num);
    if (b == 0) return p5_die(P, "Illegal division by zero");
    return PN_NUM(a / b);
  }
  if (PN_IS_INT(self) && PN_IS_INT(num)) {
    long a = PN_INT(self), b = PN_INT(num);
    if (b == 0) return p5_die(P, "Illegal division by zero");
    if (a % b == 0) return PN_NUM(a / b);
    return potion_double(P, (double)a / (double)b);
  }
  if (p5_nv(num) == 0.0) return p5_die(P, "Illegal division by zero");
  return potion_double(P, p5_nv(self) / p5_nv(num));
}

/* operands are truncated to integers, the result has the sign of the right operand */
static PN p5_rem(Potion *P, PN cl, PN self, PN num) {
  long a = p5_iv(self), b = p5_iv(num);
  long r;
  if (b == 0) return p5_die(P, "Illegal modulus zero");
  r = a % b;
  if (p5_use_integer) return PN_NUM(r); /* C remainder: the sign of the left operand */
  if (r != 0 && ((r < 0) != (b < 0))) r += b;
  return PN_NUM(r);
}

/* an integer when both are integers, the exponent is >= 0 and the result fits */
static PN p5_pow(Potion *P, PN cl, PN self, PN sup) {
  double z = pow(p5_nv(self), p5_nv(sup));
  if (PN_IS_INT(self) && PN_IS_INT(sup) && PN_INT(sup) >= 0 && fabs(z) < 4.0e18) {
    long base = PN_INT(self), e = PN_INT(sup), r = 1;
    while (e) { if (e & 1) r *= base; e >>= 1; if (e) base *= base; }
    return PN_NUM(r);
  }
  return potion_double(P, z);
}

/* + - *: the VM/JIT handle int/int inline; a double operand ends up here. Under
 * 'use integer' the operands are truncated, else the result is a double */
#define P5_ARITH(name, op, builtin) \
static PN name(Potion *P, PN cl, PN self, PN num) { \
  if (PN_IS_INT(self) && PN_IS_INT(num)) { \
    long r; \
    if (!builtin(PN_INT(self), PN_INT(num), &r)) return PN_NUM(r); \
    return potion_double(P, (double)PN_INT(self) op (double)PN_INT(num)); \
  } \
  if (p5_use_integer) return PN_NUM(p5_iv(self) op p5_iv(num)); \
  return potion_double(P, p5_nv(self) op p5_nv(num)); \
}
P5_ARITH(p5_add, +, __builtin_add_overflow)
P5_ARITH(p5_sub, -, __builtin_sub_overflow)
P5_ARITH(p5_mult, *, __builtin_mul_overflow)

static PN p5_num_string(Potion *P, PN cl, PN self) {
  char buf[40];
  if (PN_IS_INT(self)) sprintf(buf, "%ld", PN_INT(self));
  else sprintf(buf, "%.15g", ((struct PNDouble *)self)->value);
  return potion_str(P, buf);
}

void p5_number_init(Potion *P) {
  PN vts[3];
  int i;
  vts[0] = PN_VTABLE(PN_TNUMBER);
  vts[1] = PN_VTABLE(PN_TINTEGER);
  vts[2] = PN_VTABLE(PN_TDOUBLE);
  for (i = 0; i < 3; i++) {
    potion_method(vts[i], "%", p5_rem, "value=N");
    potion_method(vts[i], "/", p5_div, "value=N");
    potion_method(vts[i], "**", p5_pow, "value=N");
    potion_method(vts[i], "+", p5_add, "value=N");
    potion_method(vts[i], "-", p5_sub, "value=N");
    potion_method(vts[i], "*", p5_mult, "value=N");
    potion_method(vts[i], "string", p5_num_string, 0);
  }
  potion_method(P->lobby, "p5integer", p5_integer_pragma, "flag=o");
}
