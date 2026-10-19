/**\file lib/p5/p5table.c
  p5 list natives on top of the core PNTuple: @_ builder, a..b, map/grep/sort.

  (c) 2026 perl11.com */
#include <stdlib.h>
#include <string.h>
#include "p5.h"
#include "khash.h"
#include "table.h"

/// p5 @_ builder: the leading arguments up to the first PN_P5NOARG sentinel
/// (see p5_sub_proto in syn/syntax-p5.y); the 12 hidden parameters of a sub
/// which reads @_ are passed as separate arguments.
static PN p5_args(Potion *P, PN cl, PN self, PN a0, PN a1, PN a2, PN a3, PN a4,
                  PN a5, PN a6, PN a7, PN a8, PN a9, PN a10, PN a11) {
  PN a[12] = { a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11 };
  PN_SIZE i, n = 0, extra = 0;
  PN t, rest = PN_NIL;
  while (n < 12 && a[n] != PN_P5NOARG) n++;
  if (n == 12 && PN_IS_TUPLE(a[11]) && PN_TUPLE_LEN(a[11]) >= 1 && PN_TUPLE_AT(a[11], 0) == PN_P5REST) {
    rest = a[11]; n = 11; extra = PN_TUPLE_LEN(rest) - 1;
  }
  t = potion_tuple_with_size(P, n + extra);
  {
    struct PNTuple *d = (struct PNTuple *)potion_fwd(t);
    for (i = 0; i < n; i++) d->set[i] = a[i];
    for (i = 0; i < extra; i++) d->set[n + i] = PN_TUPLE_AT(rest, i + 1);
  }
  PN_TOUCH(t);
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
    r = PN_CLOSURE_CALL2(P, block, P->lobby, PN_TUPLE_AT(self, i)); /* the block's $_ parameter */
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
    r = PN_CLOSURE_CALL2(P, block, P->lobby, el);
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
  free((void *)a); free((void *)b);
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
/* the key of a hash slice element: perl keys are strings, but a hash built from numeric literals holds number keys */
PN potion_table_exists(Potion *, PN, PN, PN);
static PN p5_hkey(Potion *P, PN cont, PN idx) {
  PN k = p5_tostr(P, idx);
  if (PN_IS_NUM(idx) && potion_table_exists(P, PN_NIL, cont, k) != PN_TRUE && potion_table_exists(P, PN_NIL, cont, idx) == PN_TRUE) return idx;
  return k;
}
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
    PN key = p5_hkey(P, cont, idx);
    return PN_PUSH(out, potion_table_at(P, PN_NIL, cont, key));
  }
  return PN_PUSH(out, PN_NIL);
}
static PN p5_slice(Potion *P, PN cl, PN self, PN cont) {
  return p5_slice_pick(P, potion_fwd(cont), self, PN_TUP0());
}

/* INDICES.p5sliceset(container, values): @a[...] = LIST / @h{...} = LIST; the missing values are
 * undef, a list grows as needed. The indices are flattened like in p5_slice_pick. */
PN potion_table_put(Potion *, PN, PN, PN, PN);
static long p5_sliceset_at(Potion *P, PN cont, PN idx, PN vals, long vi) {
  if (PN_IS_TUPLE(idx)) {
    PN_SIZE i;
    for (i = 0; i < PN_TUPLE_LEN(idx); i++) vi = p5_sliceset_at(P, cont, PN_TUPLE_AT(idx, i), vals, vi);
    return vi;
  }
  {
    PN v = vi < (long)PN_TUPLE_LEN(vals) ? PN_TUPLE_AT(vals, vi) : PN_NIL;
    if (PN_IS_TUPLE(cont)) {
      long n = (long)PN_TUPLE_LEN(cont), k = PN_IS_INT(idx) ? PN_INT(idx) : (idx == PN_NIL ? 0 : (long)PN_DBL(idx));
      if (k < 0) k += n;
      if (k >= 0) {
        while ((long)PN_TUPLE_LEN(cont) <= k) potion_tuple_push(P, cont, PN_NIL);
        cont = potion_fwd(cont); /* a grown list may have moved */
        PN_TUPLE_AT(cont, k) = v;
        PN_TOUCH(cont);
      }
    } else if (PN_IS_PTR(cont) && PN_TYPE(cont) == PN_TTABLE) {
      potion_table_put(P, PN_NIL, cont, p5_tostr(P, idx), v);
    }
  }
  return vi + 1;
}
static PN p5_sliceset(Potion *P, PN cl, PN self, PN cont, PN vals) {
  if (!PN_IS_TUPLE(vals)) vals = vals == PN_NIL ? PN_TUP0() : PN_TUP(vals);
  p5_sliceset_at(P, potion_fwd(cont), self, vals, 0);
  return vals;
}

/* CONTAINER.p5sliceof(indices): $r->@[...] / $h->@{...} */
static PN p5_sliceof(Potion *P, PN cl, PN self, PN idx) {
  return p5_slice_pick(P, potion_fwd(self), idx, PN_TUP0());
}
/* $h->%*: the table itself; $r->$#*: the last index */
static PN p5_href(Potion *P, PN cl, PN self) { return self; }
static PN p5_lastidx(Potion *P, PN cl, PN self) { return PN_NUM((long)PN_TUPLE_LEN(self) - 1); }

/* local $h{k} / local $a[i]: CONTAINER.p5lsave(key) is (existed, value), p5lrestore(key, saved)
 * puts the value back, or deletes the key / shortens the list when the element did not exist */
PN potion_table_delete(Potion *, PN, PN, PN);
PN potion_table_exists(Potion *, PN, PN, PN);
static int p5_aidx(PN self, PN idx, long *k);
static PN p5_adelete(Potion *P, PN cl, PN self, PN idx);
static PN p5_lsave(Potion *P, PN cl, PN self, PN key) {
  PN v = p5_slice_pick(P, potion_fwd(self), key, PN_TUP0());
  long k;
  int ex = PN_IS_TUPLE(self) ? p5_aidx(self, key, &k)
         : PN_IS_PTR(self) && PN_TYPE(self) == PN_TTABLE && potion_table_exists(P, PN_NIL, self, p5_tostr(P, key)) == PN_TRUE;
  return PN_PUSH(PN_PUSH(PN_TUP0(), PN_BOOL(ex)), PN_TUPLE_AT(v, 0));
}
static PN p5_lrestore(Potion *P, PN cl, PN self, PN key, PN saved) {
  int ex = PN_IS_TUPLE(saved) && PN_TUPLE_AT(saved, 0) == PN_TRUE;
  PN val = PN_IS_TUPLE(saved) ? PN_TUPLE_AT(saved, 1) : PN_NIL;
  if (ex) {
    p5_sliceset_at(P, potion_fwd(self), key, PN_PUSH(PN_TUP0(), val), 0);
  } else if (PN_IS_TUPLE(self)) {
    long k;
    if (p5_aidx(self, key, &k)) p5_adelete(P, cl, self, key);
  } else if (PN_IS_PTR(self) && PN_TYPE(self) == PN_TTABLE) {
    potion_table_delete(P, PN_NIL, self, p5_tostr(P, key));
  }
  return PN_NIL;
}

/* %h in list context: the key, value pairs */
PN p5_table_pairs(Potion *P, PN tbl) {
  struct PNTable *t = (struct PNTable *)potion_fwd(tbl);
  PN out = PN_TUP0();
  unsigned k;
  for (k = kh_begin(t); k != kh_end(t); k++)
    if (kh_exist(PN, t, k)) { out = PN_PUSH(out, kh_key(PN, t, k)); out = PN_PUSH(out, kh_val(PN, t, k)); t = (struct PNTable *)potion_fwd(tbl); }
  return out;
}
/* LIST.p5ext(OTHER): a new list, OTHER's elements (or OTHER itself) appended: list literal flattening */
static PN p5_ext(Potion *P, PN cl, PN self, PN other) {
  PN_SIZE i, n = PN_IS_TUPLE(self) ? PN_TUPLE_LEN(self) : 0;
  PN out = PN_TUP0();
  for (i = 0; i < n; i++) out = PN_PUSH(out, PN_TUPLE_AT(self, i));
  if (PN_IS_TUPLE(other)) {
    for (i = 0; i < PN_TUPLE_LEN(other); i++) out = PN_PUSH(out, PN_TUPLE_AT(other, i));
  } else if (PN_IS_PTR(other) && PN_TYPE(other) == PN_TTABLE) { /* %h in list context */
    other = p5_table_pairs(P, other);
    for (i = 0; i < PN_TUPLE_LEN(other); i++) out = PN_PUSH(out, PN_TUPLE_AT(other, i));
  } else if (other != PN_NIL) out = PN_PUSH(out, other);
  return out;
}
/* @$r, @{EXPR}: the list a reference points to (undef: empty) */
/* @a in scalar context: the number of elements; a never assigned global array is 0 */
static PN p5_alen(Potion *P, PN cl, PN self) {
  return PN_IS_TUPLE(self) ? PN_NUM(PN_TUPLE_LEN(self)) : PN_ZERO;
}
static PN p5_aref(Potion *P, PN cl, PN self) { return PN_IS_TUPLE(self) ? self : PN_TUP0(); }

/* @a.p5pushall(LIST) / p5unshiftall: the elements of LIST (one item when it is not a list) are added; returns @a */
PN potion_tuple_unshift(Potion *, PN, PN, PN);
static PN p5_pushall(Potion *P, PN cl, PN self, PN l) {
  PN_SIZE i;
  if (!PN_IS_TUPLE(l)) return potion_tuple_push(P, self, l), self;
  for (i = 0; i < PN_TUPLE_LEN(l); i++) potion_tuple_push(P, self, PN_TUPLE_AT(l, i));
  return self;
}
static PN p5_unshiftall(Potion *P, PN cl, PN self, PN l) {
  long i;
  if (!PN_IS_TUPLE(l)) { potion_tuple_unshift(P, PN_NIL, self, l); return self; }
  for (i = (long)PN_TUPLE_LEN(l) - 1; i >= 0; i--) potion_tuple_unshift(P, PN_NIL, self, PN_TUPLE_AT(l, i));
  return self;
}
/* INDICES.p5slicedel(container): delete @h{LIST} / delete @a[LIST]; the list of the deleted values */
PN potion_table_delete(Potion *, PN, PN, PN);
static PN p5_slicedel_at(Potion *P, PN cont, PN idx, PN out) {
  if (PN_IS_TUPLE(idx)) {
    PN_SIZE i;
    for (i = 0; i < PN_TUPLE_LEN(idx); i++) out = p5_slicedel_at(P, cont, PN_TUPLE_AT(idx, i), out);
    return out;
  }
  if (PN_IS_TUPLE(cont)) return PN_PUSH(out, p5_adelete(P, PN_NIL, cont, idx));
  { PN key = p5_hkey(P, cont, idx), old = potion_table_at(P, PN_NIL, cont, key);
    potion_table_delete(P, PN_NIL, cont, key);
    return PN_PUSH(out, old); }
}
static PN p5_slicedel(Potion *P, PN cl, PN self, PN cont) {
  return p5_slicedel_at(P, potion_fwd(cont), self, PN_TUP0());
}
/* the last element of a list (splice in scalar context), undef for an empty one */
static PN p5_lastel(Potion *P, PN cl, PN self) { return PN_IS_TUPLE(self) && PN_TUPLE_LEN(self) ? PN_TUPLE_AT(self, PN_TUPLE_LEN(self) - 1) : PN_NIL; }
/* @a = f(): the value of a sub call as a list (a scalar result is a one element list) */
static PN p5_aslist(Potion *P, PN cl, PN self, PN v) { return PN_IS_TUPLE(v) ? v : PN_TUP(v); }

PN potion_tuple_pop(Potion *, PN, PN);
/* $#a = N: resize the list to LEN elements, padding with undef */
static PN p5_setlast(Potion *P, PN cl, PN self, PN len) {
  long n = PN_IS_INT(len) ? PN_INT(len) : (PN_IS_PTR(len) && PN_IS_NUM(len)) ? (long)PN_DBL(len) : 0;
  if (!PN_IS_TUPLE(self)) return self;
  if (n < 0) n = 0;
  while ((long)PN_TUPLE_LEN(self) > n) potion_tuple_pop(P, PN_NIL, self);
  while ((long)PN_TUPLE_LEN(self) < n) potion_tuple_push(P, self, PN_NIL);
  return self;
}

/* splice(@a, OFF, LEN, LIST) in place; returns the removed elements. LEN and LIST are nil when omitted */
PN potion_tuple_repeat(Potion *, PN, PN, PN);
static long p5_int_arg(PN v, long dflt) { return PN_IS_INT(v) ? PN_INT(v) : (PN_IS_PTR(v) && PN_IS_NUM(v)) ? (long)PN_DBL(v) : dflt; }
static PN p5_splice(Potion *P, PN cl, PN self, PN off, PN len, PN items) {
  long n, o, l, i;
  PN removed = PN_TUP0(), add = PN_TUP0(), tail = PN_TUP0();
  if (!PN_IS_TUPLE(self)) return removed;
  n = (long)PN_TUPLE_LEN(self);
  o = p5_int_arg(off, 0);
  if (o < 0) o += n;
  if (o < 0) o = 0;
  if (o > n) o = n;
  l = len == PN_NIL ? n - o : p5_int_arg(len, 0);
  if (l < 0) l = n - o + l;
  if (l < 0) l = 0;
  if (l > n - o) l = n - o;
  for (i = 0; i < l; i++) removed = PN_PUSH(removed, PN_TUPLE_AT(self, o + i));
  for (i = o + l; i < n; i++) tail = PN_PUSH(tail, PN_TUPLE_AT(self, i));
  if (PN_IS_TUPLE(items))
    for (i = 0; i < (long)PN_TUPLE_LEN(items); i++) {
      PN it = PN_TUPLE_AT(items, i);
      if (PN_IS_TUPLE(it)) { PN_SIZE j; for (j = 0; j < PN_TUPLE_LEN(it); j++) add = PN_PUSH(add, PN_TUPLE_AT(it, j)); }
      else add = PN_PUSH(add, it);
    }
  else if (items != PN_NIL) add = PN_PUSH(add, items);
  while ((long)PN_TUPLE_LEN(self) > o) potion_tuple_pop(P, PN_NIL, self);
  for (i = 0; i < (long)PN_TUPLE_LEN(add); i++) potion_tuple_push(P, self, PN_TUPLE_AT(add, i));
  for (i = 0; i < (long)PN_TUPLE_LEN(tail); i++) potion_tuple_push(P, self, PN_TUPLE_AT(tail, i));
  PN_TOUCH(self);
  return removed;
}

/* the value of the missing argument I of a closure: the default of its signature entry */
static PN p5_sig_default(Potion *P, PN sig, int i) {
  PN s = potion_sig_at(P, sig, i);
  if (s == PN_NIL || !PN_IS_TUPLE(s)) return PN_NIL;
  if (PN_TUPLE_LEN(s) == 3) return PN_TUPLE_AT(s, 2);
  return PN_TUPLE_LEN(s) > 1 ? potion_type_default((char)PN_INT(PN_TUPLE_AT(s, 1))) : PN_NIL;
}

/* send a message to RECV with the method CLOSURE: RECV is self, ARGS (a list or one value) the
 * arguments, the missing ones take the defaults of the signature (what an OP_CALL fills in, but
 * a call through potion_call does not) */
PN p5_call_method(Potion *P, PN closure, PN recv, PN args) {
  PN argv[16];
  PN_SIZE i, n = PN_IS_TUPLE(args) ? PN_TUPLE_LEN(args) : (args != PN_NIL);
  vPN(Closure) c = PN_CLOSURE(closure);
  PN sig = c->sig;
  PN_SIZE arity = PN_IS_TUPLE(sig) ? (PN_SIZE)potion_sig_arity(P, sig) : 0;
  if (potion_closure_is_vm(closure) && PN_IS_TUPLE(((struct PNProto *)c->data[0])->sig)) {
    PN ary = PN_TUP0();
    sig = ((struct PNProto *)c->data[0])->sig;
    for (i = 0; i < (PN_SIZE)c->arity; i++)
      ary = PN_PUSH(ary, i < n ? (PN_IS_TUPLE(args) ? PN_TUPLE_AT(args, i) : args) : p5_sig_default(P, sig, (int)i));
    return potion_vm(P, c->data[0], recv, ary, c->extra - 1, &c->data[1]);
  }
  if (n > 14) n = 14;
  argv[0] = recv;
  for (i = 0; i < n; i++) argv[i + 1] = PN_IS_TUPLE(args) ? PN_TUPLE_AT(args, i) : args;
  for (i = n; i < arity && i < 14; i++) { argv[i + 1] = p5_sig_default(P, sig, (int)i); }
  return potion_call(P, closure, (arity > n && arity <= 14 ? arity : n) + 1, argv);
}

/* CODE.p5callv(LIST): call a code value computed by any expression, &{$a[1]}(1,2), $h{cb}->(@x) */
static PN p5_callv(Potion *P, PN cl, PN self, PN args) {
  PN argv[13];
  PN_SIZE i, n = PN_IS_TUPLE(args) ? PN_TUPLE_LEN(args) : (args != PN_NIL); /* (x) is x */
  vPN(Closure) c = PN_CLOSURE(self);
  if (n > 12 && !(c->arity >= 12 && PN_IS_TUPLE(args))) n = 12;
  if (potion_closure_is_vm(self) && PN_IS_TUPLE(((struct PNProto *)c->data[0])->sig)) {
    /* potion_vm_proto's vararg heuristic replaces arguments whose type differs from the
     * optional parameter's default (the number sentinel of the hidden @_ slots) by that
     * default: strings would vanish. Hand the argument tuple to the VM directly. */
    PN ary = PN_TUP0();
    if (n > 12) { /* a hidden @_ sub: the arguments from the 12th on travel as one packed tuple */
      PN rest = PN_PUSH(PN_TUP0(), PN_P5REST);
      for (i = 0; i < 11; i++) ary = PN_PUSH(ary, PN_TUPLE_AT(args, i));
      for (i = 11; i < n; i++) rest = PN_PUSH(rest, PN_TUPLE_AT(args, i));
      ary = PN_PUSH(ary, rest);
      return potion_vm(P, c->data[0], P->lobby, ary, c->extra - 1, &c->data[1]);
    }
    for (i = 0; i < (PN_SIZE)c->arity; i++)
      ary = PN_PUSH(ary, i < n ? (PN_IS_TUPLE(args) ? PN_TUPLE_AT(args, i) : args)
                               : c->arity >= 12 ? PN_P5NOARG : PN_NIL);
    return potion_vm(P, c->data[0], P->lobby, ary, c->extra - 1, &c->data[1]);
  }
  argv[0] = P->lobby;
  for (i = 0; i < 12; i++)
    argv[i + 1] = i >= n ? PN_P5NOARG : PN_IS_TUPLE(args) ? PN_TUPLE_AT(args, i) : args;
  if (n > 12) { /* the arguments from the 12th on travel as one packed tuple */
    PN rest = PN_PUSH(PN_TUP0(), PN_P5REST);
    for (i = 11; i < n; i++) rest = PN_PUSH(rest, PN_TUPLE_AT(args, i));
    argv[12] = rest;
  }
  return potion_call(P, self, 13, argv);
}

/* LIST.p5rest(N): the elements from index N on (the slurpy tail of ($a, @rest) = LIST) */
static PN p5_rest(Potion *P, PN cl, PN self, PN from) {
  PN out = PN_TUP0();
  PN_SIZE i;
  if (!PN_IS_TUPLE(self)) return PN_IS_INT(from) && PN_INT(from) == 0 && self != PN_NIL ? PN_TUP(self) : out;
  for (i = PN_IS_INT(from) ? (PN_SIZE)PN_INT(from) : 0; i < PN_TUPLE_LEN(self); i++) out = PN_PUSH(out, PN_TUPLE_AT(self, i));
  return out;
}

/* exists/delete $a[i] (negative i counts from the end). delete sets the element to undef and
 * shrinks the list when it was the last one (and the undef tail before it); the hash versions are the core table methods */
PN potion_table_exists(Potion *, PN, PN, PN);
PN potion_table_delete(Potion *, PN, PN, PN);
static int p5_aidx(PN self, PN idx, long *k) {
  long n = (long)PN_TUPLE_LEN(self);
  *k = PN_IS_INT(idx) ? PN_INT(idx) : (PN_IS_PTR(idx) && PN_IS_NUM(idx)) ? (long)PN_DBL(idx) : 0;
  if (*k < 0) *k += n;
  return *k >= 0 && *k < n;
}
static PN p5_aexists(Potion *P, PN cl, PN self, PN idx) {
  long k;
  return PN_BOOL(p5_aidx(self, idx, &k));
}
static PN p5_adelete(Potion *P, PN cl, PN self, PN idx) {
  long k;
  PN old;
  if (!p5_aidx(self, idx, &k)) return PN_NIL;
  old = PN_TUPLE_AT(self, k);
  PN_TUPLE_AT(self, k) = PN_NIL;
  PN_TOUCH(self);
  /* perl also drops the deleted slots before it; an undef element is taken as deleted */
  if (k == (long)PN_TUPLE_LEN(self) - 1)
    while (PN_TUPLE_LEN(self) && PN_TUPLE_AT(self, PN_TUPLE_LEN(self) - 1) == PN_NIL) potion_tuple_pop(P, PN_NIL, self);
  return old;
}

void p5_table_init(Potion *P) {
  potion_method(PN_VTABLE(PN_TTUPLE), "p5exists", p5_aexists, "key=o");
  potion_method(PN_VTABLE(PN_TTUPLE), "p5delete", p5_adelete, "key=o");
  potion_method(PN_VTABLE(PN_TTABLE), "p5exists", potion_table_exists, "key=o");
  potion_method(PN_VTABLE(PN_TTABLE), "p5delete", potion_table_delete, "key=o");
  potion_method(PN_VTABLE(PN_TOBJECT), "p5rest", p5_rest, "from=o");
  potion_method(PN_VTABLE(PN_TCLOSURE), "p5callv", p5_callv, "args=o");
  PN tpl_vt = PN_VTABLE(PN_TTUPLE);
  potion_method(P->lobby, "p5args", p5_args, "a0=o,a1=o,a2=o,a3=o,a4=o,a5=o,a6=o,a7=o,a8=o,a9=o,a10=o,a11=o");
  potion_method(P->lobby, "p5range", p5_range, "lo=o,hi=o");
  potion_method(tpl_vt, "p5slice", p5_slice, "cont=o");
  potion_method(tpl_vt, "p5lsave", p5_lsave, "key=o");
  potion_method(PN_VTABLE(PN_TTABLE), "p5lsave", p5_lsave, "key=o");
  potion_method(tpl_vt, "p5lrestore", p5_lrestore, "key=o,saved=o");
  potion_method(PN_VTABLE(PN_TTABLE), "p5lrestore", p5_lrestore, "key=o,saved=o");
  potion_method(tpl_vt, "p5sliceof", p5_sliceof, "idx=o");
  potion_method(PN_VTABLE(PN_TTABLE), "p5sliceof", p5_sliceof, "idx=o");
  potion_method(PN_VTABLE(PN_TTABLE), "p5href", p5_href, 0);
  potion_method(tpl_vt, "p5lastidx", p5_lastidx, 0);
  potion_method(tpl_vt, "p5sliceset", p5_sliceset, "cont=o,vals=o");
  potion_method(tpl_vt, "p5ext", p5_ext, "other=o");
  potion_method(tpl_vt, "p5setlast", p5_setlast, "len=o");
  potion_method(tpl_vt, "p5splice", p5_splice, "off=o,len=o,items=o");
  potion_method(tpl_vt, "p5lrepeat", potion_tuple_repeat, "count=o");
  potion_method(P->lobby, "p5aslist", p5_aslist, "v=o");
  potion_method(tpl_vt, "p5aref", p5_aref, 0);
  potion_method(tpl_vt, "p5refid", p5_aref, 0); /* \\@a: the array itself, but not spread as a call argument */
  potion_method(tpl_vt, "p5lastel", p5_lastel, 0);
  potion_method(tpl_vt, "p5slicedel", p5_slicedel, "cont=o");
  potion_method(tpl_vt, "p5pushall", p5_pushall, "l=o");
  potion_method(tpl_vt, "p5unshiftall", p5_unshiftall, "l=o");
  potion_method(PN_VTABLE(PN_TNIL), "p5aref", p5_aref, 0);
  potion_method(PN_VTABLE(PN_TNIL), "p5alen", p5_alen, 0);
  potion_method(tpl_vt, "p5alen", p5_alen, 0);
  potion_method(tpl_vt, "p5each", p5_each, 0);
  potion_method(PN_VTABLE(PN_TTABLE), "p5each", p5_each, 0);
  potion_method(tpl_vt, "p5map", p5_map, "block=&");
  potion_method(tpl_vt, "p5grep", p5_grep, "block=&");
  potion_method(tpl_vt, "p5sort", p5_sort, "block=o");
  potion_define_global(P, PN_STR("$_"), PN_NIL);
  potion_define_global(P, PN_STR("$a"), PN_NIL);
  potion_define_global(P, PN_STR("$b"), PN_NIL);
}
