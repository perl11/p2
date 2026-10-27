/**\file lib/p5/p5number.c
  p5 number semantics on top of the core PNNumber (Potion keeps C integer
  division/remainder and integer-only pow): exact integer quotients stay
  integers, else a double (7/2 = 3.5); % has the sign of the right operand;
  ** gives a double for negative or huge exponents; doubles print with %.15g.
  The p2 VM and JIT always dispatch / % ** to these methods.

  (c) 2026 perl11.com */
#include <errno.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <time.h>
#include <unistd.h>
#include "p5.h"

/* $^H, perl's compile time hints (HINT_INTEGER = 0x1, see perl.h). The p5 parser
 * maps reads of $^H to p5hints(-1) and assignments to p5hints(VALUE), and
 * 'use integer' / 'no integer' to $^H |= 1 / $^H &= ~1, which the enclosing
 * block restores at its end. The number methods test the integer bit. */
#define P5_HINT_INTEGER 0x1
static intptr_t p5_hints;
#define p5_use_integer (p5_hints & P5_HINT_INTEGER)

static PN p5_hints_native(Potion *P, PN cl, PN self, PN v) {
  if (PN_IS_INT(v) && PN_INT(v) >= 0) p5_hints = PN_INT(v);
  return PN_NUM(p5_hints);
}

/* %^H, the compile time hints hash, is block scoped like $^H: the parser (p5_hh_save/restore in
 * syn/syntax-p5.y) calls p5hhpush at the start of every block and file and p5hhpop at its end, which
 * bring back the table and the $^H value of the enclosing scope. A file (require) starts clear. */
static PN p5_hh_stack;
static PN p5_hh_snaps; /* (table, hints) of the `eval STRING` sites that see compile time hints */
static int p5_hh_keep; /* the next file/eval parse keeps the hints instead of starting clear */
static PN p5_hh_push(Potion *P, PN cl, PN self, PN clear) {
  PN snap, item;
  if (p5_hh_stack == 0 || p5_hh_stack == PN_NIL) { p5_hh_stack = PN_TUP0(); potion_gc_root(P, &p5_hh_stack); }
  snap = potion_send(potion_send(P->lobby, PN_STR("%^H")), PN_STR("clone"));
  item = PN_PUSH(PN_TUP(snap), PN_NUM(p5_hints));
  p5_hh_stack = PN_PUSH(p5_hh_stack, item);
  if (clear == PN_TRUE && p5_hh_keep) p5_hh_keep = 0;
  else if (clear == PN_TRUE) {
    potion_define_global(P, PN_STR("%^H"), potion_send(PN_TUP0(), PN_STR("table")));
    p5_hints = 0;
  }
  return PN_NIL;
}
static PN p5_hh_pop(Potion *P, PN cl, PN self) {
  PN item, rest = PN_TUP0();
  long i, n;
  if (p5_hh_stack == 0 || p5_hh_stack == PN_NIL || !(n = PN_TUPLE_LEN(p5_hh_stack))) return PN_NIL;
  item = PN_TUPLE_AT(p5_hh_stack, n - 1);
  for (i = 0; i < n - 1; i++) rest = PN_PUSH(rest, PN_TUPLE_AT(p5_hh_stack, i));
  p5_hh_stack = rest;
  potion_define_global(P, PN_STR("%^H"), PN_TUPLE_AT(item, 0));
  p5_hints = PN_INT(PN_TUPLE_AT(item, 1));
  return PN_NIL;
}

/* the parser calls p5hhsnap at an `eval STRING`: when hints are active the eval site gets the index of
 * a snapshot (nil: nothing to keep); p5evalstrh (lib/p5/p5eval.c) enters it around the eval */
static PN p5_hh_snap(Potion *P, PN cl, PN self) {
  PN tbl = potion_send(potion_send(P->lobby, PN_STR("%^H")), PN_STR("clone"));
  if (PN_INT(potion_send(tbl, PN_STR("length"))) == 0 && p5_hints == 0) return PN_NIL;
  if (p5_hh_snaps == 0 || p5_hh_snaps == PN_NIL) { p5_hh_snaps = PN_TUP0(); potion_gc_root(P, &p5_hh_snaps); }
  p5_hh_snaps = PN_PUSH(p5_hh_snaps, PN_PUSH(PN_TUP(tbl), PN_NUM(p5_hints)));
  return PN_NUM(PN_TUPLE_LEN(p5_hh_snaps) - 1);
}
void p5_hh_enter(Potion *P, PN idx) {
  PN item;
  p5_hh_push(P, PN_NIL, PN_NIL, PN_FALSE);
  if (!PN_IS_INT(idx) || !p5_hh_snaps || p5_hh_snaps == PN_NIL || PN_INT(idx) >= (long)PN_TUPLE_LEN(p5_hh_snaps)) return;
  item = PN_TUPLE_AT(p5_hh_snaps, PN_INT(idx));
  potion_define_global(P, PN_STR("%^H"), potion_send(PN_TUPLE_AT(item, 0), PN_STR("clone")));
  p5_hints = PN_INT(PN_TUPLE_AT(item, 1));
  p5_hh_keep = 1;
}
void p5_hh_leave(Potion *P) { p5_hh_keep = 0; p5_hh_pop(P, PN_NIL, PN_NIL); }

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

/* A == B for the cases the identity compare misses: numbers (boxed doubles) and
 * numeric strings. References and other objects are only equal if identical. */
static int p5_numlike(PN n) {
  return !PN_IS_PTR(n) || PN_IS_STR(n) || PN_IS_NUM(n);
}
static PN p5_numeq(Potion *P, PN cl, PN self, PN a, PN b) {
  if (a == b) return PN_BOOL(!((PN_IS_PTR(a) && PN_IS_NUM(a) && !PN_IS_INT(a) && isnan(PN_DBL(a))) || (PN_IS_STR(a) && isnan(p5_nv(a))))); /* NaN != NaN, also the string "NaN" */
  if (PN_IS_PTR(a) && !p5_numlike(a)) { int h; PN r = p5_ovl_numeq(P, a, b, &h); if (h) return r; }
  if (!p5_numlike(a) || !p5_numlike(b)) return PN_FALSE;
  if ((PN_IS_BIG(a) || PN_IS_BIG(b)) && (PN_IS_INT(a) || PN_IS_BIG(a)) && (PN_IS_INT(b) || PN_IS_BIG(b)))
    return PN_BOOL(potion_big_cmp(a, b) == 0); /* integers beyond the tagged range: exact */
  return PN_BOOL(p5_nv(a) == p5_nv(b));
}

/* operand as an integer: doubles truncate (use integer) */
static intptr_t p5_iv(PN n) {
  return PN_IS_INT(n) ? PN_INT(n) : (intptr_t)p5_nv(n);
}

static PN p5_die(Potion *P, const char *msg) {
  potion_send(P->lobby, PN_STR("die"), PN_STR(msg));
  return PN_NIL;
}

static PN p5_div(Potion *P, PN cl, PN self, PN num) {
  if (p5_use_integer) {
    intptr_t a = p5_iv(self), b = p5_iv(num);
    if (b == 0) return p5_die(P, "Illegal division by zero");
    return PN_NUM(a / b);
  }
  if (PN_IS_INT(self) && PN_IS_INT(num)) {
    intptr_t a = PN_INT(self), b = PN_INT(num);
    if (b == 0) return p5_die(P, "Illegal division by zero");
    if (a % b == 0) return PN_NUM(a / b);
    return potion_double(P, (double)a / (double)b);
  }
  if ((PN_IS_BIG(self) || PN_IS_BIG(num)) && (PN_IS_INT(self) || PN_IS_BIG(self)) && (PN_IS_INT(num) || PN_IS_BIG(num))) {
    PN m = potion_big_mod(P, self, num), q;
    if (m == PN_NIL) return p5_die(P, "Illegal division by zero");
    if (m == PN_ZERO && (q = potion_big_div(P, self, num)) != PN_NIL) return q; /* exact: stays an integer */
  }
  if (p5_nv(num) == 0.0) return p5_die(P, "Illegal division by zero");
  return potion_double(P, p5_nv(self) / p5_nv(num));
}

/* operands are truncated to integers, the result has the sign of the right operand */
static PN p5_rem(Potion *P, PN cl, PN self, PN num) {
  intptr_t a, b, r;
  if (!p5_use_integer && (PN_IS_BIG(self) || PN_IS_BIG(num)) && (PN_IS_INT(self) || PN_IS_BIG(self)) && (PN_IS_INT(num) || PN_IS_BIG(num))) {
    PN m = potion_big_mod(P, self, num); /* the sign of the right operand */
    if (m == PN_NIL) return p5_die(P, "Illegal modulus zero");
    return m;
  }
  if (!p5_use_integer) { /* an operand beyond the UV range: perl's fmod path on rounded absolute values */
    double dl = p5_nv(self), dr = p5_nv(num);
    if (fabs(dl) >= 18446744073709551616.0 || fabs(dr) >= 18446744073709551616.0) {
      int lneg = dl < 0, rneg = dr < 0;
      double ans;
      dl = floor(fabs(dl) + 0.5); dr = floor(fabs(dr) + 0.5);
      if (dr == 0.0) return p5_die(P, "Illegal modulus zero");
      ans = fmod(dl, dr);
      if (lneg != rneg && ans != 0.0) ans = dr - ans;
      if (rneg) ans = -ans;
      return potion_double(P, ans);
    }
  }
  a = p5_iv(self); b = p5_iv(num);
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
    intptr_t base = PN_INT(self), e = PN_INT(sup), r = 1;
    while (e) { if (e & 1) r *= base; e >>= 1; if (e) base *= base; }
    return PN_NUM(r);
  }
  return potion_double(P, z);
}

/* + - *: the VM/JIT handle int/int inline; a double operand ends up here. Under
 * 'use integer' the operands are truncated, else the result is a double */
/* perl's integers are 64 bit (IV/UV): a bigger result is a double */
static PN p5_bigres(Potion *P, PN r) {
  if (PN_IS_BIG(r) && !potion_big_in64(r)) return potion_double(P, PN_DBL(r));
  return r;
}
#define P5_ARITH(name, op, builtin, bigfn) \
static PN name(Potion *P, PN cl, PN self, PN num) { \
  if (PN_IS_INT(self) && PN_IS_INT(num)) { \
    intptr_t r; \
    if (!builtin(PN_INT(self), PN_INT(num), &r) && r >= -(INTPTR_MAX >> 1) - 1 && r <= (INTPTR_MAX >> 1)) return PN_NUM(r); \
    if (!p5_use_integer) return p5_bigres(P, bigfn(P, self, num)); /* an integer overflow: a bigint */ \
    return potion_double(P, (double)PN_INT(self) op (double)PN_INT(num)); \
  } \
  if (!p5_use_integer && (PN_IS_INT(self) || PN_IS_BIG(self)) && (PN_IS_INT(num) || PN_IS_BIG(num))) return p5_bigres(P, bigfn(P, self, num)); \
  if (p5_use_integer) return PN_NUM(p5_iv(self) op p5_iv(num)); \
  return potion_double(P, p5_nv(self) op p5_nv(num)); \
}
P5_ARITH(p5_add, +, __builtin_add_overflow, potion_big_add)
P5_ARITH(p5_sub, -, __builtin_sub_overflow, potion_big_sub)
P5_ARITH(p5_mult, *, __builtin_mul_overflow, potion_big_mul)

/* LIST.p5join: the p5 '.' operator, all items stringified and concatenated */
static PN p5_join_items(Potion *P, PN cl, PN self) {
  PN_SIZE i, n, len = 0;
  char *buf, *q;
  PN r;
  if (PN_IS_PTR(self) && PN_TYPE(self) == PN_TTABLE) self = p5_table_pairs(P, self);
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
      PN_TOUCH(res); /* res may have been promoted by the allocations above */
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
    intptr_t v;
    const char *h = str;
    while (*h == ' ' || *h == '\t' || *h == '\n') h++;
    if (*h == '-' || *h == '+') h++;
    if (h[0] == '0' && (h[1] == 'x' || h[1] == 'X') && isxdigit((unsigned char)h[2])) { /* cperl: "0x100" + 1 is 257 */
      v = strtoll(h + 2, &e, 16);
      return PN_NUM(str[h - str - 1] == '-' && h > str ? -v : v);
    }
    if (h[0] == '0' && (h[1] == 'b' || h[1] == 'B') && (h[2] == '0' || h[2] == '1')) { /* cperl: "0b100" + 1 is 5 */
      v = strtoll(h + 2, &e, 2);
      return PN_NUM(h > str && h[-1] == '-' ? -v : v);
    }
    errno = 0;
    v = strtoll(str, &e, 10);
    if (e != str && *e != '.' && *e != 'e' && *e != 'E') {
      if (errno == ERANGE && *str != '-') { /* up to 2**64-1 an integer like perl's UV, else a double */
        unsigned long long u = strtoull(str, NULL, 10);
        if (errno != ERANGE) return potion_big_from_u64(P, (uint64_t)u);
      } else if (errno != ERANGE) return potion_big_from_i64(P, (int64_t)v);
      return potion_double(P, strtod(str, NULL));
    }
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
static PN p5_bitres(Potion *P, uintptr_t r) {
  if (p5_use_integer) return potion_big_from_i64(P, (int64_t)r);
  return potion_big_from_u64(P, (uint64_t)r); /* an integer, or a bigint beyond the tagged range */
}
static uintptr_t p5_uv(PN n) {
  double d;
  if (PN_IS_INT(n)) return (uintptr_t)PN_INT(n);
  if (PN_IS_BIG(n)) { uint64_t u; if (potion_big_to_u64(n, &u)) return (uintptr_t)u; return UINTPTR_MAX; }
  d = p5_nv(n);
  if (d >= 18446744073709551615.0) return UINTPTR_MAX;
  if (d >= 18446744073709551615.0) return UINTPTR_MAX; /* ~0 is the double 2**64: all ones */
  return d < 0 ? (uintptr_t)(intptr_t)d : (uintptr_t)d;
}
static PN p5_bor(Potion *P, PN cl, PN self, PN a, PN b) { return p5_bitres(P, p5_uv(a) | p5_uv(b)); }
static PN p5_band(Potion *P, PN cl, PN self, PN a, PN b) { return p5_bitres(P, p5_uv(a) & p5_uv(b)); }
static PN p5_bxor(Potion *P, PN cl, PN self, PN a, PN b) { return p5_bitres(P, p5_uv(a) ^ p5_uv(b)); }

/* ~ << >> with perl's unsigned 64 bit semantics (signed under use integer) */
static PN p5_bnot(Potion *P, PN cl, PN self) { return p5_bitres(P, ~p5_uv(self)); }
static PN p5_bshl(Potion *P, PN cl, PN self, PN n) {
  long c = PN_IS_NUM(n) ? (long)PN_DBL(n) : 0;
  if (c < 0) return p5_bitres(P, c <= -64 ? 0 : p5_uv(self) >> -c);
  return p5_bitres(P, c >= 64 ? 0 : p5_uv(self) << c);
}
static PN p5_bshr(Potion *P, PN cl, PN self, PN n) {
  long c = PN_IS_NUM(n) ? (long)PN_DBL(n) : 0;
  if (c < 0) return p5_bitres(P, c <= -64 ? 0 : p5_uv(self) << -c);
  if (p5_use_integer) return PN_NUM(c >= 64 ? ((intptr_t)p5_uv(self) < 0 ? -1 : 0) : (intptr_t)p5_uv(self) >> c);
  return p5_bitres(P, c >= 64 ? 0 : p5_uv(self) >> c);
}

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
  if (PN_IS_INT(n) || PN_IS_BIG(n)) return n;
  d = trunc(PN_DBL(n));
  if (fabs(d) < 4.0e18) return PN_NUM((intptr_t)d);
  return potion_double(P, d);
}
/* abs: -2**62 and bigints may need a bigint result */
static PN p5_abs(Potion *P, PN cl, PN self) {
  PN n = p5_rnum(P, self);
  if (PN_IS_INT(n)) return PN_INT(n) < 0 ? potion_big_neg(P, n) : n;
  if (PN_IS_BIG(n)) return PN_DBL(n) < 0 ? potion_big_neg(P, n) : n;
  return potion_double(P, fabs(PN_DBL(n)));
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
  unsigned long long v;
  if (!PN_IS_PTR(self) || !PN_IS_STR(self)) return PN_NUM(0);
  str = PN_STR_PTR(self);
  while (*str == ' ') str++;
  if (base == 8) {
    if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) { base = 16; str += 2; }
    else if (str[0] == '0' && (str[1] == 'b' || str[1] == 'B')) { base = 2; str += 2; }
    else if (str[0] == 'x' || str[0] == 'X') { base = 16; str++; }
    else if (str[0] == 'b' || str[0] == 'B') { base = 2; str++; }
  } else if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) str += 2;
  v = strtoull(str, &end, base);
  return potion_big_from_u64(P, (uint64_t)v);
}
static PN p5_hex(Potion *P, PN cl, PN self) { return p5_base_str(P, self, 16); }
static PN p5_oct(Potion *P, PN cl, PN self) { return p5_base_str(P, self, 8); }

/* time and sleep */
static PN p5_time(Potion *P, PN cl, PN self) { return PN_NUM((intptr_t)time(NULL)); }
static PN p5_sleep(Potion *P, PN cl, PN self) {
  intptr_t n = p5_iv(self);
  if (n > 0) sleep((unsigned)n);
  return PN_NUM(n);
}

static PN p5_num_string(Potion *P, PN cl, PN self) {
  char buf[40];
  if (PN_IS_INT(self)) sprintf(buf, "%lld", (long long)PN_INT(self));
  else if (PN_IS_BIG(self)) return potion_big_string(P, self, 10);
  else if (((struct PNDouble *)self)->value == 0) strcpy(buf, "0"); /* -0.0 prints 0 */
  else sprintf(buf, "%.15g", ((struct PNDouble *)self)->value);
  return potion_str(P, buf);
}

void p5_number_init(Potion *P) {
  PN vts[4];
  int i;
  vts[0] = PN_VTABLE(PN_TNUMBER);
  vts[1] = PN_VTABLE(PN_TINTEGER);
  vts[2] = PN_VTABLE(PN_TDOUBLE);
  vts[3] = PN_VTABLE(PN_TBIGINT);
  for (i = 0; i < 4; i++) {
    potion_method(vts[i], "%", p5_rem, "value=N");
    potion_method(vts[i], "/", p5_div, "value=N");
    potion_method(vts[i], "**", p5_pow, "value=N");
    potion_method(vts[i], "+", p5_add, "value=N");
    potion_method(vts[i], "-", p5_sub, "value=N");
    potion_method(vts[i], "*", p5_mult, "value=N");
    potion_method(vts[i], "string", p5_num_string, 0);
  }
  potion_method(P->lobby, "time", p5_time, 0);
  potion_method(P->lobby, "p5numeq", p5_numeq, "a=o,b=o");
  {
    PN rv[7] = { PN_VTABLE(PN_TNUMBER), PN_VTABLE(PN_TINTEGER), PN_VTABLE(PN_TDOUBLE),
                 PN_VTABLE(PN_TSTRING), PN_VTABLE(PN_TNIL), PN_VTABLE(PN_TBOOLEAN), PN_VTABLE(PN_TBIGINT) };
    for (i = 0; i < 7; i++) {
      potion_method(rv[i], "int", p5_int, 0);
      potion_method(rv[i], "abs", p5_abs, 0);
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
  {
    PN bv[4] = { PN_VTABLE(PN_TNUMBER), PN_VTABLE(PN_TINTEGER), PN_VTABLE(PN_TDOUBLE), PN_VTABLE(PN_TBIGINT) };
    int bi;
    for (bi = 0; bi < 4; bi++) {
      potion_method(bv[bi], "~", p5_bnot, 0);
      potion_method(bv[bi], "<<", p5_bshl, "value=o");
      potion_method(bv[bi], ">>", p5_bshr, "value=o");
    }
  }
  potion_method(P->lobby, "p5bor", p5_bor, "a=o,b=o");
  potion_method(P->lobby, "p5band", p5_band, "a=o,b=o");
  potion_method(P->lobby, "p5lxor", p5_lxor, "a=o,b=o");
  potion_method(P->lobby, "p5bxor", p5_bxor, "a=o,b=o");
  potion_method(P->lobby, "p5hints", p5_hints_native, "value=o");
  potion_method(P->lobby, "p5hhpush", p5_hh_push, "clear=o");
  potion_method(P->lobby, "p5hhpop", p5_hh_pop, 0);
  potion_method(P->lobby, "p5hhsnap", p5_hh_snap, 0);
  potion_define_global(P, PN_STR("%^H"), potion_send(PN_TUP0(), PN_STR("table")));
  potion_method(PN_VTABLE(PN_TTUPLE), "p5join", p5_join_items, 0);
  potion_method(PN_VTABLE(PN_TTABLE), "p5join", p5_join_items, 0);
  potion_method(PN_VTABLE(PN_TSTRING), "p5join", p5_join_items, 0); /* print reverse("abc"): one item */
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
