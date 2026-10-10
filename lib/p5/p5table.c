/**\file lib/p5/p5table.c
  p5 list natives on top of the core PNTuple: @_ builder, a..b, map/grep/sort.

  (c) 2026 perl11 org */
#include <stdlib.h>
#include <string.h>
#include "p5.h"

/// p5 @_ builder: the leading elements of the argument tuple up to the first
/// PN_P5NOARG sentinel (see p5_sub_proto in syn/syntax-p5.y). Not a 12-arg
/// native: the JIT mishandles calls with that many arguments.
static PN p5_args(Potion *P, PN cl, PN self, PN args) {
  PN t = PN_TUP0();
  PN_SIZE i;
  for (i = 0; i < PN_TUPLE_LEN(args) && PN_TUPLE_AT(args, i) != PN_P5NOARG; i++)
    t = PN_PUSH(t, PN_TUPLE_AT(args, i));
  return t;
}

/* ---- p5 map / grep / sort: the block reads $_ (map, grep) or $a and $b (sort),
 * which are lobby globals pre-created in p5_table_init ---- */
static void p5_set_global(Potion *P, const char *name, PN val) {
  potion_define_global(P, potion_str(P, name), val);
}

/* LIST.p5map(block): concatenation of the block's values, a list result is flattened */
static PN p5_map(Potion *P, PN cl, PN self, PN block) {
  PN out = PN_TUP0();
  PN_SIZE i;
  if (!PN_IS_TUPLE(self)) self = PN_TUP(self);
  if (PN_TYPE(block) != PN_TCLOSURE) return out;
  for (i = 0; i < PN_TUPLE_LEN(self); i++) {
    PN r;
    p5_set_global(P, "$_", PN_TUPLE_AT(self, i));
    r = PN_CLOSURE_CALL2(P, block, P->lobby, PN_NIL);
    if (PN_IS_TUPLE(r)) {
      PN_SIZE j;
      for (j = 0; j < PN_TUPLE_LEN(r); j++) out = PN_PUSH(out, PN_TUPLE_AT(r, j));
    } else {
      out = PN_PUSH(out, r);
    }
  }
  return out;
}

static PN p5_grep(Potion *P, PN cl, PN self, PN block) {
  PN out = PN_TUP0();
  PN_SIZE i;
  if (!PN_IS_TUPLE(self)) self = PN_TUP(self);
  if (PN_TYPE(block) != PN_TCLOSURE) return out;
  for (i = 0; i < PN_TUPLE_LEN(self); i++) {
    PN el = PN_TUPLE_AT(self, i), r;
    p5_set_global(P, "$_", el);
    r = PN_CLOSURE_CALL2(P, block, P->lobby, PN_NIL);
    if (PN_TEST(r) && r != PN_ZERO && r != PN_STR0) out = PN_PUSH(out, el);
  }
  return out;
}

static int p5_sort_cmp(Potion *P, PN block, PN x, PN y) {
  if (PN_TYPE(block) == PN_TCLOSURE) {
    PN r;
    p5_set_global(P, "$a", x);
    p5_set_global(P, "$b", y);
    r = PN_CLOSURE_CALL2(P, block, P->lobby, PN_NIL);
    return PN_IS_NUM(r) ? (PN_DBL(r) < 0 ? -1 : PN_DBL(r) > 0 ? 1 : 0) : 0;
  } else {
    PN sx = PN_IS_STR(x) ? x : potion_send(x, PN_string);
    PN sy = PN_IS_STR(y) ? y : potion_send(y, PN_string);
    int c = PN_IS_STR(sx) && PN_IS_STR(sy) ? strcmp(PN_STR_PTR(sx), PN_STR_PTR(sy)) : 0;
    return c < 0 ? -1 : c > 0 ? 1 : 0;
  }
}

/* LIST.p5sort(cmp): stable merge sort; cmp is a block using $a/$b, or nil for
 * string order */
static PN p5_sort(Potion *P, PN cl, PN self, PN block) {
  PN_SIZE n, w, i;
  PN *a, *b, *t;
  PN out;
  if (!PN_IS_TUPLE(self)) self = PN_TUP(self);
  n = PN_TUPLE_LEN(self);
  out = potion_tuple_with_size(P, n);
  if (n < 2) {
    if (n) PN_TUPLE_AT(out, 0) = PN_TUPLE_AT(self, 0);
    return out;
  }
  a = malloc(sizeof(PN) * n); b = malloc(sizeof(PN) * n);
  for (i = 0; i < n; i++) a[i] = PN_TUPLE_AT(self, i);
  for (w = 1; w < n; w *= 2) {
    for (i = 0; i < n; i += 2 * w) {
      PN_SIZE l = i, m = i + w < n ? i + w : n, r = i + 2 * w < n ? i + 2 * w : n, k = i, p = m;
      while (l < m && p < r)
        b[k++] = p5_sort_cmp(P, block, a[l], a[p]) <= 0 ? a[l++] : a[p++];
      while (l < m) b[k++] = a[l++];
      while (p < r) b[k++] = a[p++];
    }
    t = a; a = b; b = t;
  }
  for (i = 0; i < n; i++) PN_TUPLE_AT(out, i) = a[i];
  free(a); free(b);
  return out;
}

/// p5 a..b: the tuple of integers from lo to hi (empty if lo > hi)
static PN p5_range(Potion *P, PN cl, PN self, PN lo, PN hi) {
  PN t;
  long i, n;
  if (!PN_IS_INT(lo) || !PN_IS_INT(hi) || PN_INT(lo) > PN_INT(hi))
    return PN_TUP0();
  n = PN_INT(hi) - PN_INT(lo) + 1;
  t = potion_tuple_with_size(P, n); /* one allocation, not n pushes */
  for (i = 0; i < n; i++)
    PN_TUPLE_AT(t, i) = PN_NUM(PN_INT(lo) + i);
  return t;
}

void p5_table_init(Potion *P) {
  PN tpl_vt = PN_VTABLE(PN_TTUPLE);
  potion_method(P->lobby, "p5args", p5_args, "args=o");
  potion_method(P->lobby, "p5range", p5_range, "lo=o,hi=o");
  potion_method(tpl_vt, "p5map", p5_map, "block=&");
  potion_method(tpl_vt, "p5grep", p5_grep, "block=&");
  potion_method(tpl_vt, "p5sort", p5_sort, "block=o");
  potion_define_global(P, PN_STR("$_"), PN_NIL);
  potion_define_global(P, PN_STR("$a"), PN_NIL);
  potion_define_global(P, PN_STR("$b"), PN_NIL);
}
