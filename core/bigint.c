/**\file core/bigint.c
  Arbitrary precision integers with libtommath (3rd/libtommath).

  A PNBigInt exists only for integers that do not fit a tagged integer (|n| >= 2**62): every
  operation normalizes its result, so an integer is a tagged one or a PNBigInt, never both.
  The limbs live inline in the GC object (struct PNBigInt), the collector needs no finalizer:
  operands are read through a temporary mp_int view onto those limbs, results are computed in a
  malloc'ed mp_int, copied into a new GC object and cleared.

  (c) 2026 perl11.com */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <tommath.h>
#include "potion.h"
#include "internal.h"

/* one operand: a view onto the limbs of a PNBigInt (not to be modified or cleared), or a temporary
 * mp_int made from a tagged integer (to be cleared) */
typedef struct { mp_int m; int owned; } big_t;

static int big_set_u64(mp_int *m, uint64_t v) {
  int r;
  if ((r = mp_set_int(m, (unsigned long)(v >> 32))) != MP_OKAY) return r;
  if ((r = mp_mul_2d(m, 32, m)) != MP_OKAY) return r;
  {
    mp_int lo;
    if ((r = mp_init(&lo)) != MP_OKAY) return r;
    if ((r = mp_set_int(&lo, (unsigned long)(v & 0xFFFFFFFFu))) == MP_OKAY) r = mp_add(m, &lo, m);
    mp_clear(&lo);
  }
  return r;
}

static int big_get(PN n, big_t *b) {
  if (PN_IS_INT(n)) {
    intptr_t v = PN_INT(n);
    if (mp_init(&b->m) != MP_OKAY) potion_allocation_error();
    if (big_set_u64(&b->m, v < 0 ? (uint64_t)(-(v + 1)) + 1 : (uint64_t)v) != MP_OKAY) potion_allocation_error();
    if (v < 0) mp_neg(&b->m, &b->m);
    b->owned = 1;
    return 1;
  }
  if (PN_IS_BIG(n)) {
    struct PNBigInt *i = (struct PNBigInt *)potion_fwd(n);
    b->m.dp = (mp_digit *)i->dp;
    b->m.used = i->used;
    b->m.alloc = i->used;
    b->m.sign = i->sign;
    b->owned = 0;
    return 1;
  }
  return 0;
}
static void big_put(big_t *b) { if (b->owned) mp_clear(&b->m); }

/* the double value of a magnitude with sign */
static double big_double(mp_int *m) {
  double d = 0;
  int i;
  for (i = m->used - 1; i >= 0; i--) d = d * (double)((mp_word)1 << DIGIT_BIT) + (double)m->dp[i];
  return m->sign == MP_NEG ? -d : d;
}

/* a tagged integer if it fits, else a new PNBigInt */
static PN big_norm(Potion *P, mp_int *m) {
  if (mp_count_bits(m) <= 62) {
    uint64_t v = 0;
    int i;
    for (i = m->used - 1; i >= 0; i--) v = (v << DIGIT_BIT) | (uint64_t)m->dp[i];
    return PN_NUM(m->sign == MP_NEG ? -(intptr_t)v : (intptr_t)v);
  }
  {
    size_t bytes = ((size_t)m->used * sizeof(mp_digit) + 7) & ~(size_t)7;
    vPN(BigInt) b = PN_ALLOC_N(PN_TBIGINT, struct PNBigInt, bytes);
    b->sign = m->sign == MP_NEG;
    b->used = m->used;
    b->dbytes = (int)bytes;
    b->pad = 0;
    memset(b->dp, 0, bytes);
    memcpy(b->dp, m->dp, (size_t)m->used * sizeof(mp_digit));
    b->value = big_double(m);
    return (PN)b;
  }
}

PN potion_big_from_u64(Potion *P, uint64_t v) {
  mp_int m;
  PN r;
  if (v <= (uint64_t)(INTPTR_MAX >> 1)) return PN_NUM((intptr_t)v);
  if (mp_init(&m) != MP_OKAY || big_set_u64(&m, v) != MP_OKAY) potion_allocation_error();
  r = big_norm(P, &m);
  mp_clear(&m);
  return r;
}
PN potion_big_from_i64(Potion *P, int64_t v) {
  mp_int m;
  PN r;
  if (v >= -(int64_t)(INTPTR_MAX >> 1) - 1 && v <= (int64_t)(INTPTR_MAX >> 1)) return PN_NUM((intptr_t)v);
  if (mp_init(&m) != MP_OKAY || big_set_u64(&m, v < 0 ? (uint64_t)(-(v + 1)) + 1 : (uint64_t)v) != MP_OKAY) potion_allocation_error();
  if (v < 0) mp_neg(&m, &m);
  r = big_norm(P, &m);
  mp_clear(&m);
  return r;
}
PN potion_big_from_str(Potion *P, const char *s, int radix) {
  mp_int m;
  PN r;
  if (mp_init(&m) != MP_OKAY) potion_allocation_error();
  if (mp_read_radix(&m, s, radix) != MP_OKAY) { mp_clear(&m); return PN_NIL; }
  r = big_norm(P, &m);
  mp_clear(&m);
  return r;
}

/* the low 64 bits of the value, two's complement for a negative one; 0 if it does not fit 64 bits */
int potion_big_to_u64(PN n, uint64_t *out) {
  big_t a;
  mp_int t;
  uint64_t v = 0;
  int i, ok = 0;
  if (PN_IS_INT(n)) { *out = (uint64_t)(int64_t)PN_INT(n); return 1; }
  if (!big_get(n, &a)) return 0;
  if (mp_count_bits(&a.m) <= 64) {
    t = a.m;
    for (i = t.used - 1; i >= 0; i--) v = (v << DIGIT_BIT) | (uint64_t)t.dp[i];
    *out = a.m.sign == MP_NEG ? (uint64_t)(-(int64_t)v) : v;
    ok = 1;
  }
  big_put(&a);
  return ok;
}

/* 1 if the integer is within perl's IV/UV range: -2**63 .. 2**64-1 */
int potion_big_in64(PN n) {
  big_t a;
  int bits, ok;
  if (PN_IS_INT(n)) return 1;
  if (!big_get(n, &a)) return 0;
  bits = mp_count_bits(&a.m);
  if (a.m.sign != MP_NEG) ok = bits <= 64;
  else if (bits <= 63) ok = 1;
  else if (bits == 64) { /* only -2**63 */
    mp_int t;
    ok = mp_init(&t) == MP_OKAY && mp_2expt(&t, 63) == MP_OKAY && mp_neg(&t, &t) == MP_OKAY && mp_cmp(&a.m, &t) == MP_EQ;
    mp_clear(&t);
  } else ok = 0;
  big_put(&a);
  return ok;
}

/* result = a OP b with libtommath's binary function f */
typedef int (*big_binop)(mp_int *, mp_int *, mp_int *);
static PN big_op2(Potion *P, PN x, PN y, big_binop f, int nonneg) {
  big_t a, b;
  mp_int r;
  PN res = PN_NIL;
  if (!big_get(x, &a)) return PN_NIL;
  if (!big_get(y, &b)) { big_put(&a); return PN_NIL; }
  if (nonneg && (a.m.sign == MP_NEG || b.m.sign == MP_NEG)) { big_put(&a); big_put(&b); return PN_NIL; }
  if (mp_init(&r) != MP_OKAY) potion_allocation_error();
  if (f(&a.m, &b.m, &r) == MP_OKAY) res = big_norm(P, &r);
  mp_clear(&r);
  big_put(&a); big_put(&b);
  return res;
}
PN potion_big_add(Potion *P, PN a, PN b) { return big_op2(P, a, b, mp_add, 0); }
PN potion_big_sub(Potion *P, PN a, PN b) { return big_op2(P, a, b, mp_sub, 0); }
PN potion_big_mul(Potion *P, PN a, PN b) { return big_op2(P, a, b, mp_mul, 0); }
PN potion_big_and(Potion *P, PN a, PN b) { return big_op2(P, a, b, mp_and, 1); }
PN potion_big_or(Potion *P, PN a, PN b)  { return big_op2(P, a, b, mp_or, 1); }
PN potion_big_xor(Potion *P, PN a, PN b) { return big_op2(P, a, b, mp_xor, 1); }
PN potion_big_mod(Potion *P, PN x, PN y) {
  big_t b;
  PN res;
  if (!big_get(y, &b)) return PN_NIL;
  if (mp_iszero(&b.m)) { big_put(&b); return PN_NIL; }
  big_put(&b);
  res = big_op2(P, x, y, mp_mod, 0);
  return res;
}
PN potion_big_div(Potion *P, PN x, PN y) {
  big_t a, b;
  mp_int q, r;
  PN res = PN_NIL;
  if (!big_get(x, &a)) return PN_NIL;
  if (!big_get(y, &b)) { big_put(&a); return PN_NIL; }
  if (!mp_iszero(&b.m)) {
    if (mp_init(&q) != MP_OKAY || mp_init(&r) != MP_OKAY) potion_allocation_error();
    if (mp_div(&a.m, &b.m, &q, &r) == MP_OKAY) res = big_norm(P, &q);
    mp_clear(&q); mp_clear(&r);
  }
  big_put(&a); big_put(&b);
  return res;
}
PN potion_big_neg(Potion *P, PN x) {
  big_t a;
  mp_int r;
  PN res = PN_NIL;
  if (!big_get(x, &a)) return PN_NIL;
  if (mp_init(&r) != MP_OKAY) potion_allocation_error();
  if (mp_neg(&a.m, &r) == MP_OKAY) res = big_norm(P, &r);
  mp_clear(&r);
  big_put(&a);
  return res;
}
PN potion_big_pow(Potion *P, PN x, unsigned long e) {
  big_t a;
  mp_int r;
  PN res = PN_NIL;
  if (!big_get(x, &a)) return PN_NIL;
  if (mp_init(&r) != MP_OKAY) potion_allocation_error();
  if (e <= MP_MASK && mp_expt_d(&a.m, (mp_digit)e, &r) == MP_OKAY) res = big_norm(P, &r);
  mp_clear(&r);
  big_put(&a);
  return res;
}
PN potion_big_shl(Potion *P, PN x, unsigned long n) {
  big_t a;
  mp_int r;
  PN res = PN_NIL;
  if (!big_get(x, &a)) return PN_NIL;
  if (mp_init(&r) != MP_OKAY) potion_allocation_error();
  if (mp_mul_2d(&a.m, (int)n, &r) == MP_OKAY) res = big_norm(P, &r);
  mp_clear(&r);
  big_put(&a);
  return res;
}
PN potion_big_shr(Potion *P, PN x, unsigned long n) {
  big_t a;
  mp_int r;
  PN res = PN_NIL;
  if (!big_get(x, &a)) return PN_NIL;
  if (mp_init(&r) != MP_OKAY) potion_allocation_error();
  if (mp_div_2d(&a.m, (int)n, &r, NULL) == MP_OKAY) res = big_norm(P, &r);
  mp_clear(&r);
  big_put(&a);
  return res;
}
int potion_big_cmp(PN x, PN y) {
  big_t a, b;
  int c = 0;
  if (!big_get(x, &a)) return 0;
  if (!big_get(y, &b)) { big_put(&a); return 0; }
  c = mp_cmp(&a.m, &b.m);
  big_put(&a); big_put(&b);
  return c < 0 ? -1 : c > 0;
}
PN potion_big_string(Potion *P, PN x, int radix) {
  big_t a;
  PN res = PN_NIL;
  int size = 0;
  if (!big_get(x, &a)) return PN_NIL;
  if (mp_radix_size(&a.m, radix, &size) == MP_OKAY) {
    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf) potion_allocation_error();
    if (mp_toradix(&a.m, buf, radix) == MP_OKAY) res = potion_str(P, buf);
    free(buf);
  }
  big_put(&a);
  return res;
}

/* ---- the BigInt methods of core potion (p2 overrides + - * / % with perl semantics, see lib/p5) ---- */
static PN big_m_string(Potion *P, PN cl, PN self) { return potion_big_string(P, self, 10); }
static PN big_m_add(Potion *P, PN cl, PN self, PN o) { PN r = PN_IS_DBL(o) ? potion_double(P, PN_DBL(self) + PN_DBL(o)) : potion_big_add(P, self, o); return r == PN_NIL ? PN_NIL : r; }
static PN big_m_sub(Potion *P, PN cl, PN self, PN o) { PN r = PN_IS_DBL(o) ? potion_double(P, PN_DBL(self) - PN_DBL(o)) : potion_big_sub(P, self, o); return r == PN_NIL ? PN_NIL : r; }
static PN big_m_mult(Potion *P, PN cl, PN self, PN o) { PN r = PN_IS_DBL(o) ? potion_double(P, PN_DBL(self) * PN_DBL(o)) : potion_big_mul(P, self, o); return r == PN_NIL ? PN_NIL : r; }
static PN big_m_div(Potion *P, PN cl, PN self, PN o) { return PN_IS_DBL(o) ? potion_double(P, PN_DBL(self) / PN_DBL(o)) : potion_big_div(P, self, o); }
static PN big_m_rem(Potion *P, PN cl, PN self, PN o) { return potion_big_mod(P, self, o); }
static PN big_m_cmp(Potion *P, PN cl, PN self, PN o) {
  if (PN_IS_DBL(o)) { double x = PN_DBL(self), y = PN_DBL(o); return PN_NUM(x < y ? -1 : x > y); }
  return PN_NUM(potion_big_cmp(self, o));
}
static PN big_m_neg(Potion *P, PN cl, PN self) { return potion_big_neg(P, self); }

void potion_bigint_init(Potion *P) {
  PN vt = PN_VTABLE(PN_TBIGINT);
  potion_method(vt, "string", big_m_string, 0);
  potion_method(vt, "+", big_m_add, "value=N");
  potion_method(vt, "-", big_m_sub, "value=N");
  potion_method(vt, "*", big_m_mult, "value=N");
  potion_method(vt, "/", big_m_div, "value=N");
  potion_method(vt, "%", big_m_rem, "value=N");
  potion_method(vt, "cmp", big_m_cmp, "value=N");
  potion_method(vt, "neg", big_m_neg, 0);
}
