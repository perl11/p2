/**\file lib/p5/p5table.c
  p5 list natives on top of the core PNTuple: @_ builder, a..b, map/grep/sort.

  (c) 2026 perl11 org */
#include <stdlib.h>
#include <string.h>
#include "p5.h"
#include "khash.h"
#include "table.h"

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
    PN sx = p5_tostr(P, x);
    PN sy = p5_tostr(P, y);
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

/* ---- each / list helpers ---- */

/* iterator positions of each(@a) / each(%h), by the container's uniq (objects move) */
#define P5_EACH_MAX 64
static struct { PNUniq uniq; unsigned pos; } p5_iter[P5_EACH_MAX];
static int p5_iter_n;

static unsigned *p5_iter_slot(PN c) {
  PNUniq u = ((struct PNObject *)c)->uniq;
  int i;
  for (i = 0; i < p5_iter_n; i++)
    if (p5_iter[i].uniq == u) return &p5_iter[i].pos;
  if (p5_iter_n == P5_EACH_MAX) p5_iter_n = 0; /* drop the oldest iterators */
  p5_iter[p5_iter_n].uniq = u;
  p5_iter[p5_iter_n].pos = 0;
  return &p5_iter[p5_iter_n++].pos;
}

/* each: the next (key, value) of a hash or (index, element) of an array, () at the end */
static PN p5_each(Potion *P, PN cl, PN self) {
  unsigned *pos;
  PN r;
  self = potion_fwd(self);
  if (!PN_IS_PTR(self)) return PN_TUP0();
  pos = p5_iter_slot(self);
  if (PN_IS_TUPLE(self)) {
    if (*pos >= PN_TUPLE_LEN(self)) { *pos = 0; return PN_TUP0(); }
    r = PN_PUSH(PN_PUSH(PN_TUP0(), PN_NUM(*pos)), PN_TUPLE_AT(self, *pos));
    (*pos)++;
    return r;
  }
  if (PN_TYPE(self) == PN_TTABLE) {
    struct PNTable *t = (struct PNTable *)self;
    unsigned k;
    for (k = *pos; k != kh_end(t); k++)
      if (kh_exist(PN, t, k)) {
        r = PN_PUSH(PN_PUSH(PN_TUP0(), kh_key(PN, t, k)), kh_val(PN, t, k));
        *pos = k + 1;
        return r;
      }
    *pos = 0;
  }
  return PN_TUP0();
}

/* INDICES.p5slice(container): the slice @a[...] / @h{...}; nested lists in the
 * index list (ranges) are flattened, negative array indices count from the end */
static PN p5_slice_pick(Potion *P, PN cont, PN idx, PN out) {
  if (PN_IS_TUPLE(idx)) {
    PN_SIZE i;
    for (i = 0; i < PN_TUPLE_LEN(idx); i++) out = p5_slice_pick(P, cont, PN_TUPLE_AT(idx, i), out);
    return out;
  }
  if (PN_IS_TUPLE(cont)) {
    long n = (long)PN_TUPLE_LEN(cont), k = PN_IS_INT(idx) ? PN_INT(idx) : (idx == PN_NIL ? 0 : (long)PN_DBL(idx));
    if (k < 0) k += n;
    return PN_PUSH(out, (k >= 0 && k < n) ? PN_TUPLE_AT(cont, k) : PN_NIL);
  }
  if (PN_IS_PTR(cont) && PN_TYPE(cont) == PN_TTABLE) {
    PN key = p5_tostr(P, idx);
    return PN_PUSH(out, potion_table_at(P, PN_NIL, cont, key));
  }
  return PN_PUSH(out, PN_NIL);
}
static PN p5_slice(Potion *P, PN cl, PN self, PN cont) {
  return p5_slice_pick(P, potion_fwd(cont), self, PN_TUP0());
}

/* LIST.p5ext(OTHER): a new list, OTHER's elements (or OTHER itself) appended: list literal flattening */
static PN p5_ext(Potion *P, PN cl, PN self, PN other) {
  PN_SIZE i, n = PN_IS_TUPLE(self) ? PN_TUPLE_LEN(self) : 0;
  PN out = PN_TUP0();
  for (i = 0; i < n; i++) out = PN_PUSH(out, PN_TUPLE_AT(self, i));
  if (PN_IS_TUPLE(other)) {
    for (i = 0; i < PN_TUPLE_LEN(other); i++) out = PN_PUSH(out, PN_TUPLE_AT(other, i));
  } else if (PN_IS_PTR(other) && PN_TYPE(other) == PN_TTABLE) { /* %h in list context: key, value pairs */
    struct PNTable *t = (struct PNTable *)potion_fwd(other);
    unsigned k;
    for (k = kh_begin(t); k != kh_end(t); k++)
      if (kh_exist(PN, t, k)) { out = PN_PUSH(out, kh_key(PN, t, k)); out = PN_PUSH(out, kh_val(PN, t, k)); }
  } else if (other != PN_NIL) out = PN_PUSH(out, other);
  return out;
}
/* @$r, @{EXPR}: the list a reference points to (undef: empty) */
static PN p5_aref(Potion *P, PN cl, PN self) { return PN_IS_TUPLE(self) ? self : PN_TUP0(); }

/* CODE.p5callv(LIST): call a code value computed by any expression, &{$a[1]}(1,2), $h{cb}->(@x) */
static PN p5_callv(Potion *P, PN cl, PN self, PN args) {
  PN argv[13];
  PN_SIZE i, n = PN_IS_TUPLE(args) ? PN_TUPLE_LEN(args) : (args != PN_NIL); /* (x) is x */
  vPN(Closure) c = PN_CLOSURE(self);
  if (n > 12) n = 12;
  if (c->method == (PN_F)potion_vm_proto && PN_IS_TUPLE(((struct PNProto *)c->data[0])->sig)) {
    /* potion_vm_proto's vararg heuristic replaces arguments whose type differs from the
     * optional parameter's default (the number sentinel of the hidden @_ slots) by that
     * default: strings would vanish. Hand the argument tuple to the VM directly. */
    PN ary = PN_TUP0();
    for (i = 0; i < (PN_SIZE)c->arity; i++)
      ary = PN_PUSH(ary, i < n ? (PN_IS_TUPLE(args) ? PN_TUPLE_AT(args, i) : args)
                               : c->arity >= 12 ? PN_P5NOARG : PN_NIL);
    return potion_vm(P, c->data[0], P->lobby, ary, c->extra - 1, &c->data[1]);
  }
  argv[0] = P->lobby;
  for (i = 0; i < 12; i++)
    argv[i + 1] = i >= n ? PN_P5NOARG : PN_IS_TUPLE(args) ? PN_TUPLE_AT(args, i) : args;
  return potion_call(P, self, 13, argv);
}

void p5_table_init(Potion *P) {
  potion_method(PN_VTABLE(PN_TCLOSURE), "p5callv", p5_callv, "args=o");
  PN tpl_vt = PN_VTABLE(PN_TTUPLE);
  potion_method(P->lobby, "p5args", p5_args, "args=o");
  potion_method(P->lobby, "p5range", p5_range, "lo=o,hi=o");
  potion_method(tpl_vt, "p5slice", p5_slice, "cont=o");
  potion_method(tpl_vt, "p5ext", p5_ext, "other=o");
  potion_method(tpl_vt, "p5aref", p5_aref, 0);
  potion_method(PN_VTABLE(PN_TNIL), "p5aref", p5_aref, 0);
  potion_method(tpl_vt, "p5each", p5_each, 0);
  potion_method(PN_VTABLE(PN_TTABLE), "p5each", p5_each, 0);
  potion_method(tpl_vt, "p5map", p5_map, "block=&");
  potion_method(tpl_vt, "p5grep", p5_grep, "block=&");
  potion_method(tpl_vt, "p5sort", p5_sort, "block=o");
  potion_define_global(P, PN_STR("$_"), PN_NIL);
  potion_define_global(P, PN_STR("$a"), PN_NIL);
  potion_define_global(P, PN_STR("$b"), PN_NIL);
}
