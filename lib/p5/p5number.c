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
#include <string.h>
#include <limits.h>
#include <time.h>
#include <unistd.h>
#include "p5.h"

/* $^H, perl's compile time hints (HINT_INTEGER = 0x1, see perl.h). The p5 parser
 * maps reads of $^H to p5hints(-1) and assignments to p5hints(VALUE), and
 * 'use integer' / 'no integer' to $^H |= 1 / $^H &= ~1, which the enclosing
 * block restores at its end. The number methods test the integer bit. */
#define P5_HINT_INTEGER 0x1
static long p5_hints;
#define p5_use_integer (p5_hints & P5_HINT_INTEGER)

static PN p5_hints_native(Potion *P, PN cl, PN self, PN v) {
  if (PN_IS_INT(v) && PN_INT(v) >= 0) p5_hints = PN_INT(v);
  return PN_NUM(p5_hints);
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

/* LIST.p5join: the p5 '.' operator, all items stringified and concatenated */
static PN p5_join_items(Potion *P, PN cl, PN self) {
  PN_SIZE i, n, len = 0;
  char *buf, *q;
  PN r;
  if (!PN_IS_TUPLE(self)) self = PN_TUP(self);
  n = PN_TUPLE_LEN(self);
  /* stringify first into a GC tuple (the sends may allocate and move strings) */
  {
    PN res = potion_tuple_with_size(P, n);
    for (i = 0; i < n; i++) {
      PN x = PN_TUPLE_AT(self, i);
      x = p5_tostr(P, x);
      if (!PN_IS_STR(x)) x = PN_STR("");
      PN_TUPLE_AT(res, i) = x;
    }
    for (i = 0; i < n; i++) len += PN_STR_LEN(PN_TUPLE_AT(res, i));
    buf = q = malloc(len + 1);
    if (!buf) potion_allocation_error();
    for (i = 0; i < n; i++) {
      memcpy(q, PN_STR_PTR(PN_TUPLE_AT(res, i)), PN_STR_LEN(PN_TUPLE_AT(res, i)));
      q += PN_STR_LEN(PN_TUPLE_AT(res, i));
    }
  }
  r = potion_str2(P, buf, len);
  free(buf);
  return r;
}

/* a string/undef/bool receiver in numeric context: parse its numeric prefix */
static PN p5_numify(Potion *P, PN n) {
  if (PN_IS_INT(n)) return n;
  if (PN_IS_PTR(n) && PN_IS_STR(n)) {
    char *e;
    const char *str = PN_STR_PTR(n);
    long v = strtol(str, &e, 10);
    if (e != str && *e != '.' && *e != 'e' && *e != 'E') return PN_NUM(v);
    return potion_double(P, strtod(str, NULL));
  }
  if (PN_IS_PTR(n) && PN_IS_NUM(n)) return n;
  return PN_NUM(n == PN_TRUE ? 1 : 0);
}

#define P5_NUMRCV(name, fn) \
static PN name(Potion *P, PN cl, PN self, PN num) { return fn(P, cl, p5_numify(P, self), num); }
P5_NUMRCV(p5_str_add, p5_add)
P5_NUMRCV(p5_str_sub, p5_sub)
P5_NUMRCV(p5_str_mult, p5_mult)
P5_NUMRCV(p5_str_div, p5_div)
P5_NUMRCV(p5_str_rem, p5_rem)
P5_NUMRCV(p5_str_pow, p5_pow)

/* | & ^ on integers (perl: unsigned, signed under use integer) */
static PN p5_bitres(Potion *P, unsigned long r) {
  if (p5_use_integer) return PN_NUM((long)r);
  if (r <= (unsigned long)PN_INT(LONG_MAX)) return PN_NUM((long)r);
  return potion_double(P, (double)r);
}
static unsigned long p5_uv(PN n) {
  double d;
  if (PN_IS_INT(n)) return (unsigned long)PN_INT(n);
  d = p5_nv(n);
  return d < 0 ? (unsigned long)(long)d : (unsigned long)d;
}
static PN p5_bor(Potion *P, PN cl, PN self, PN a, PN b) { return p5_bitres(P, p5_uv(a) | p5_uv(b)); }
static PN p5_band(Potion *P, PN cl, PN self, PN a, PN b) { return p5_bitres(P, p5_uv(a) & p5_uv(b)); }
static PN p5_bxor(Potion *P, PN cl, PN self, PN a, PN b) { return p5_bitres(P, p5_uv(a) ^ p5_uv(b)); }

/* a xor b: logical, both operands evaluated */
static PN p5_lxor(Potion *P, PN cl, PN self, PN a, PN b) {
  int ta = PN_TEST(a) && a != PN_ZERO && a != PN_STR0, tb = PN_TEST(b) && b != PN_ZERO && b != PN_STR0;
  return ta != tb ? PN_TRUE : PN_FALSE;
}

/* perl's numeric builtins, receiver style like 'int $x' / 'sqrt(2)': the receiver
 * is numified (strings, undef and booleans too) */
static PN p5_rnum(Potion *P, PN self) { return p5_numify(P, self); }

static PN p5_int(Potion *P, PN cl, PN self) {
  PN n = p5_rnum(P, self);
  double d;
  if (PN_IS_INT(n)) return n;
  d = trunc(PN_DBL(n));
  if (fabs(d) < 4.0e18) return PN_NUM((long)d);
  return potion_double(P, d);
}
#define P5_MATH1(name, expr) \
static PN name(Potion *P, PN cl, PN self) { double x = PN_DBL(p5_rnum(P, self)); return potion_double(P, expr); }
P5_MATH1(p5_sqrt, sqrt(x))
P5_MATH1(p5_sin, sin(x))
P5_MATH1(p5_cos, cos(x))
P5_MATH1(p5_exp, exp(x))

static PN p5_log(Potion *P, PN cl, PN self) {
  double x = PN_DBL(p5_rnum(P, self));
  if (x <= 0.0) return p5_die(P, "Can't take log of 0");
  return potion_double(P, log(x));
}

/* hex("ff"), oct("0x1f") / oct("755") / oct("0b11") */
static PN p5_base_str(Potion *P, PN self, int base) {
  const char *str;
  char *end;
  unsigned long v;
  if (!PN_IS_PTR(self) || !PN_IS_STR(self)) return PN_NUM(0);
  str = PN_STR_PTR(self);
  while (*str == ' ') str++;
  if (base == 8) {
    if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) { base = 16; str += 2; }
    else if (str[0] == '0' && (str[1] == 'b' || str[1] == 'B')) { base = 2; str += 2; }
    else if (str[0] == 'x' || str[0] == 'X') { base = 16; str++; }
    else if (str[0] == 'b' || str[0] == 'B') { base = 2; str++; }
  } else if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) str += 2;
  v = strtoul(str, &end, base);
  return p5_bitres(P, v);
}
static PN p5_hex(Potion *P, PN cl, PN self) { return p5_base_str(P, self, 16); }
static PN p5_oct(Potion *P, PN cl, PN self) { return p5_base_str(P, self, 8); }

/* time and sleep */
static PN p5_time(Potion *P, PN cl, PN self) { return PN_NUM((long)time(NULL)); }
static PN p5_sleep(Potion *P, PN cl, PN self) {
  long n = p5_iv(self);
  if (n > 0) sleep((unsigned)n);
  return PN_NUM(n);
}

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
  potion_method(P->lobby, "time", p5_time, 0);
  {
    PN rv[6] = { PN_VTABLE(PN_TNUMBER), PN_VTABLE(PN_TINTEGER), PN_VTABLE(PN_TDOUBLE),
                 PN_VTABLE(PN_TSTRING), PN_VTABLE(PN_TNIL), PN_VTABLE(PN_TBOOLEAN) };
    for (i = 0; i < 6; i++) {
      potion_method(rv[i], "int", p5_int, 0);
      potion_method(rv[i], "sqrt", p5_sqrt, 0);
      potion_method(rv[i], "sin", p5_sin, 0);
      potion_method(rv[i], "cos", p5_cos, 0);
      potion_method(rv[i], "exp", p5_exp, 0);
      potion_method(rv[i], "log", p5_log, 0);
      potion_method(rv[i], "sleep", p5_sleep, 0);
      potion_method(rv[i], "hex", p5_hex, 0);
      potion_method(rv[i], "oct", p5_oct, 0);
    }
  }
  potion_method(P->lobby, "p5bor", p5_bor, "a=o,b=o");
  potion_method(P->lobby, "p5band", p5_band, "a=o,b=o");
  potion_method(P->lobby, "p5lxor", p5_lxor, "a=o,b=o");
  potion_method(P->lobby, "p5bxor", p5_bxor, "a=o,b=o");
  potion_method(P->lobby, "p5hints", p5_hints_native, "value=o");
  potion_method(PN_VTABLE(PN_TTUPLE), "p5join", p5_join_items, 0);
  /* '+' is numeric in p5, strings concatenate with '.': string, undef and bool
   * receivers numify */
  {
    PN rv[3] = { PN_VTABLE(PN_TSTRING), PN_VTABLE(PN_TNIL), PN_VTABLE(PN_TBOOLEAN) };
    for (i = 0; i < 3; i++) {
      potion_method(rv[i], "+", p5_str_add, "value=o");
      potion_method(rv[i], "-", p5_str_sub, "value=o");
      potion_method(rv[i], "*", p5_str_mult, "value=o");
      potion_method(rv[i], "/", p5_str_div, "value=o");
      potion_method(rv[i], "%", p5_str_rem, "value=o");
      potion_method(rv[i], "**", p5_str_pow, "value=o");
    }
  }
}
