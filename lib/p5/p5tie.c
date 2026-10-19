/**\file lib/p5/p5tie.c
  p5 tie: a tied scalar, array or hash variable holds a Tie object, which forwards the
  container operations to the methods of the tie class (TIESCALAR/FETCH/STORE, TIEARRAY/
  FETCHSIZE/PUSH.., TIEHASH/FIRSTKEY/NEXTKEY..).

    TieScalar  p5tfetch(x) / p5tstore(old, v) calls from the compiled code, reads and
               assignments of a tied scalar name (the compiler knows which names were tied)
    TieArray   the container messages: at put length p5alen p5pushall p5unshiftall pop shift
               p5splice p5delete p5exists, a list read is p5tlist
    TieHash    at put exists delete keys values p5tlist length

  The object is the tie class instance returned by TIE*; tied(VAR) returns it. A plain value
  passes through p5tfetch / p5tlist unchanged, so untie only has to assign undef (or the old
  value) to the variable.

  (c) 2026 perl11.com */
#include <stdio.h>
#include <string.h>
#include "p5.h"

static PN p5_tsc_vt, p5_tar_vt, p5_thv_vt;

#define TIE_OBJ(self) (((struct PNObject *)(self))->ivars[0])

static int tie_is(Potion *P, PN v, PN vt) {
  return PN_IS_PTR(v) && PN_TYPE(v) == potion_class_type(P, vt);
}
static PN tie_call(Potion *P, PN obj, const char *m, PN args) {
  return potion_send(obj, potion_str(P, "p5meth"), potion_str(P, m), args);
}
static PN tie_call0(Potion *P, PN obj, const char *m) { return tie_call(P, obj, m, PN_TUP0()); }
static PN tie_call1(Potion *P, PN obj, const char *m, PN a) { return tie_call(P, obj, m, PN_TUP(a)); }
static PN tie_call2(Potion *P, PN obj, const char *m, PN a, PN b) { return tie_call(P, obj, m, PN_PUSH(PN_TUP(a), b)); }
static int tie_can(Potion *P, PN obj, const char *m) {
  PN r = tie_call1(P, obj, "can", potion_str(P, m));
  return PN_IS_PTR(r) && PN_TYPE(r) == PN_TCLOSURE;
}
static long tie_num(PN v) { return PN_IS_NUM(v) ? (long)PN_DBL(v) : 0; }

/* p5tie(kind, class, args): CLASS->TIESCALAR/TIEARRAY/TIEHASH(args), wrapped in the Tie object */
static PN p5_tie(Potion *P, PN cl, PN self, PN kind, PN klass, PN args) {
  const char *k = PN_STR_PTR(kind), *ctor = k[0] == '$' ? "TIESCALAR" : k[0] == '@' ? "TIEARRAY" : "TIEHASH";
  PN obj = tie_call(P, klass, ctor, PN_IS_TUPLE(args) ? args : PN_TUP0()), t;
  if (obj == PN_NIL || obj == PN_FALSE) return PN_NIL;
  t = potion_object_new(P, PN_NIL, k[0] == '$' ? p5_tsc_vt : k[0] == '@' ? p5_tar_vt : p5_thv_vt);
  TIE_OBJ(t) = obj;
  PN_TOUCH(t);
  return t;
}
/* the tie object of VAR (tied), nil for an untied variable */
static PN p5_tobj(Potion *P, PN cl, PN self, PN x) {
  return tie_is(P, x, p5_tsc_vt) || tie_is(P, x, p5_tar_vt) || tie_is(P, x, p5_thv_vt) ? TIE_OBJ(x) : PN_NIL;
}
/* untie VAR: UNTIE and DESTROY of the tie object; the value the variable gets back (undef, an
 * empty list or hash), or the value itself when it was not tied */
static PN p5_untie(Potion *P, PN cl, PN self, PN x) {
  PN obj = p5_tobj(P, cl, self, x);
  if (obj == PN_NIL) return x;
  if (tie_can(P, obj, "UNTIE")) tie_call0(P, obj, "UNTIE");
  if (tie_can(P, obj, "DESTROY")) tie_call0(P, obj, "DESTROY");
  return tie_is(P, x, p5_tar_vt) ? PN_TUP0() : tie_is(P, x, p5_thv_vt) ? potion_table_empty(P) : PN_NIL;
}
/* a read of a tied scalar */
static PN p5_tfetch(Potion *P, PN cl, PN self, PN x) {
  return tie_is(P, x, p5_tsc_vt) ? tie_call0(P, TIE_OBJ(x), "FETCH") : x;
}
/* $x = v for a tied name: STORE through the Tie object, which stays in the variable; else v */
static PN p5_tstore(Potion *P, PN cl, PN self, PN old, PN v) {
  if (!tie_is(P, old, p5_tsc_vt)) return v;
  tie_call1(P, TIE_OBJ(old), "STORE", v);
  return old;
}
/* @a = LIST / %h = LIST for a tied name: CLEAR, then STORE/PUSH of every item */
static PN p5_tassign(Potion *P, PN cl, PN self, PN old, PN v) {
  PN obj, items;
  PN_SIZE i;
  if (tie_is(P, old, p5_tar_vt)) {
    obj = TIE_OBJ(old);
    tie_call0(P, obj, "CLEAR");
    items = PN_IS_TUPLE(v) ? v : PN_TUP(v);
    if (tie_can(P, obj, "EXTEND")) tie_call1(P, obj, "EXTEND", PN_NUM(PN_TUPLE_LEN(items)));
    for (i = 0; i < PN_TUPLE_LEN(items); i++) tie_call2(P, obj, "STORE", PN_NUM(i), PN_TUPLE_AT(items, i));
    return old;
  }
  if (tie_is(P, old, p5_thv_vt)) {
    obj = TIE_OBJ(old);
    tie_call0(P, obj, "CLEAR");
    if (PN_IS_PTR(v) && PN_TYPE(v) == PN_TTABLE) items = p5_table_pairs(P, v);
    else items = PN_IS_TUPLE(v) ? v : PN_TUP(v);
    for (i = 0; i + 1 < PN_TUPLE_LEN(items) + 1; i += 2)
      tie_call2(P, obj, "STORE", PN_TUPLE_AT(items, i), i + 1 < PN_TUPLE_LEN(items) ? PN_TUPLE_AT(items, i + 1) : PN_NIL);
    return old;
  }
  return v;
}

static PN p5_tfetch_string(Potion *P, PN cl, PN self) { return p5_tostr(P, tie_call0(P, TIE_OBJ(self), "FETCH")); }

/* ---- arrays ---- */
static long tar_size(Potion *P, PN self) { return tie_num(tie_call0(P, TIE_OBJ(self), "FETCHSIZE")); }
static PN tar_at(Potion *P, PN cl, PN self, PN i) {
  long n = tie_num(i);
  if (n < 0) n += tar_size(P, self);
  return tie_call1(P, TIE_OBJ(self), "FETCH", PN_NUM(n));
}
static PN tar_put(Potion *P, PN cl, PN self, PN i, PN v) {
  long n = tie_num(i);
  if (n < 0) n += tar_size(P, self);
  tie_call2(P, TIE_OBJ(self), "STORE", PN_NUM(n), v);
  return v;
}
static PN tar_length(Potion *P, PN cl, PN self) { return PN_NUM(tar_size(P, self)); } /* the element count: $#a is length - 1 */
static PN tar_alen(Potion *P, PN cl, PN self) { return PN_NUM(tar_size(P, self)); }
static PN tar_list(Potion *P, PN cl, PN self) {
  long i, n = tar_size(P, self);
  PN out = PN_TUP0();
  for (i = 0; i < n; i++) out = PN_PUSH(out, tie_call1(P, TIE_OBJ(self), "FETCH", PN_NUM(i)));
  return out;
}
static PN tar_push(Potion *P, PN cl, PN self, PN l) {
  tie_call(P, TIE_OBJ(self), "PUSH", PN_IS_TUPLE(l) ? l : PN_TUP(l));
  return self;
}
static PN tar_unshift(Potion *P, PN cl, PN self, PN l) {
  tie_call(P, TIE_OBJ(self), "UNSHIFT", PN_IS_TUPLE(l) ? l : PN_TUP(l));
  return self;
}
static PN tar_pop(Potion *P, PN cl, PN self) { return tie_call0(P, TIE_OBJ(self), "POP"); }
static PN tar_shift(Potion *P, PN cl, PN self) { return tie_call0(P, TIE_OBJ(self), "SHIFT"); }
static PN tar_splice(Potion *P, PN cl, PN self, PN off, PN len, PN list) {
  PN args = PN_TUP0();
  PN_SIZE i;
  args = PN_PUSH(args, off);
  args = PN_PUSH(args, len);
  if (PN_IS_TUPLE(list)) for (i = 0; i < PN_TUPLE_LEN(list); i++) args = PN_PUSH(args, PN_TUPLE_AT(list, i));
  return tie_call(P, TIE_OBJ(self), "SPLICE", args);
}
static PN tar_delete(Potion *P, PN cl, PN self, PN i) { return tie_call1(P, TIE_OBJ(self), "DELETE", i); }
static PN tar_exists(Potion *P, PN cl, PN self, PN i) { return tie_call1(P, TIE_OBJ(self), "EXISTS", i); }
/* list operations on the materialized elements */
#define TAR_FWD0(name, msg) static PN name(Potion *P, PN cl, PN self) { return potion_send(tar_list(P, cl, self), potion_str(P, msg)); }
#define TAR_FWD1(name, msg) static PN name(Potion *P, PN cl, PN self, PN a) { return potion_send(tar_list(P, cl, self), potion_str(P, msg), a); }
TAR_FWD1(tar_joins, "p5joins")
TAR_FWD0(tar_join, "p5join")
TAR_FWD1(tar_map, "p5map")
TAR_FWD1(tar_grep, "p5grep")
TAR_FWD0(tar_reverse, "reverse")
TAR_FWD1(tar_slice, "p5slice")
TAR_FWD0(tar_clone, "clone")
TAR_FWD0(tar_string, "string")

/* ---- hashes ---- */
static PN thv_keys(Potion *P, PN cl, PN self) {
  PN obj = TIE_OBJ(self), out = PN_TUP0(), k = tie_call0(P, obj, "FIRSTKEY");
  int guard = 0;
  while (k != PN_NIL && guard++ < 1000000) {
    out = PN_PUSH(out, k);
    k = tie_call1(P, obj, "NEXTKEY", PN_TUPLE_AT(out, PN_TUPLE_LEN(out) - 1));
  }
  return out;
}
static PN thv_list(Potion *P, PN cl, PN self) {
  PN keys = thv_keys(P, cl, self), out = PN_TUP0();
  PN_SIZE i;
  for (i = 0; i < PN_TUPLE_LEN(keys); i++) {
    out = PN_PUSH(out, PN_TUPLE_AT(keys, i));
    out = PN_PUSH(out, tie_call1(P, TIE_OBJ(self), "FETCH", PN_TUPLE_AT(keys, i)));
  }
  return out;
}
static PN thv_values(Potion *P, PN cl, PN self) {
  PN keys = thv_keys(P, cl, self), out = PN_TUP0();
  PN_SIZE i;
  for (i = 0; i < PN_TUPLE_LEN(keys); i++) out = PN_PUSH(out, tie_call1(P, TIE_OBJ(self), "FETCH", PN_TUPLE_AT(keys, i)));
  return out;
}
static PN thv_at(Potion *P, PN cl, PN self, PN k) { return tie_call1(P, TIE_OBJ(self), "FETCH", k); }
static PN thv_put(Potion *P, PN cl, PN self, PN k, PN v) { tie_call2(P, TIE_OBJ(self), "STORE", k, v); return v; }
static PN thv_exists(Potion *P, PN cl, PN self, PN k) { return tie_call1(P, TIE_OBJ(self), "EXISTS", k); }
static PN thv_delete(Potion *P, PN cl, PN self, PN k) { return tie_call1(P, TIE_OBJ(self), "DELETE", k); }
static PN thv_length(Potion *P, PN cl, PN self) { return PN_NUM(PN_TUPLE_LEN(thv_keys(P, cl, self))); }
/* scalar(%h): SCALAR if the class has it, else whether there are keys */
static PN thv_scalar(Potion *P, PN cl, PN self) {
  if (tie_can(P, TIE_OBJ(self), "SCALAR")) return tie_call0(P, TIE_OBJ(self), "SCALAR");
  return tie_call0(P, TIE_OBJ(self), "FIRSTKEY") != PN_NIL ? PN_NUM(1) : PN_NUM(0);
}
static PN thv_string(Potion *P, PN cl, PN self) { return potion_str(P, "HASH"); }

/* %h / @a in list context: a Tie object gives its elements, any other value itself */
static PN p5_tlist(Potion *P, PN cl, PN self, PN x) {
  if (tie_is(P, x, p5_tar_vt)) return tar_list(P, cl, x);
  if (tie_is(P, x, p5_thv_vt)) return thv_list(P, cl, x);
  return x;
}

void p5_tie_init(Potion *P) {
  p5_tsc_vt = potion_class(P, 0, 0, PN_TUP(PN_STR("obj")));
  p5_tar_vt = potion_class(P, 0, 0, PN_TUP(PN_STR("obj")));
  p5_thv_vt = potion_class(P, 0, 0, PN_TUP(PN_STR("obj")));
  potion_gc_root(P, &p5_tsc_vt);
  potion_gc_root(P, &p5_tar_vt);
  potion_gc_root(P, &p5_thv_vt);
  potion_method(P->lobby, "p5tie", p5_tie, "kind=o,class=o,args=o");
  potion_method(P->lobby, "p5tobj", p5_tobj, "x=o");
  potion_method(P->lobby, "p5untie", p5_untie, "x=o");
  potion_method(P->lobby, "p5tfetch", p5_tfetch, "x=o");
  potion_method(P->lobby, "p5tstore", p5_tstore, "old=o,v=o");
  potion_method(P->lobby, "p5tassign", p5_tassign, "old=o,v=o");
  potion_method(P->lobby, "p5tlist", p5_tlist, "x=o");

  potion_method(p5_tar_vt, "at", tar_at, "i=o");
  potion_method(p5_tar_vt, "put", tar_put, "i=o,v=o");
  potion_method(p5_tar_vt, "length", tar_length, 0);
  potion_method(p5_tar_vt, "p5alen", tar_alen, 0);
  potion_method(p5_tar_vt, "p5pushall", tar_push, "l=o");
  potion_method(p5_tar_vt, "p5unshiftall", tar_unshift, "l=o");
  potion_method(p5_tar_vt, "pop", tar_pop, 0);
  potion_method(p5_tar_vt, "shift", tar_shift, 0);
  potion_method(p5_tar_vt, "p5splice", tar_splice, "off=o,len=o,list=o");
  potion_method(p5_tar_vt, "p5delete", tar_delete, "i=o");
  potion_method(p5_tar_vt, "p5exists", tar_exists, "i=o");
  potion_method(p5_tar_vt, "p5joins", tar_joins, "sep=o");
  potion_method(p5_tar_vt, "p5join", tar_join, 0);
  potion_method(p5_tar_vt, "p5map", tar_map, "f=o");
  potion_method(p5_tar_vt, "p5grep", tar_grep, "f=o");
  potion_method(p5_tar_vt, "reverse", tar_reverse, 0);
  potion_method(p5_tar_vt, "p5slice", tar_slice, "i=o");
  potion_method(p5_tar_vt, "clone", tar_clone, 0);
  potion_method(p5_tar_vt, "string", tar_string, 0);

  potion_method(p5_thv_vt, "at", thv_at, "k=o");
  potion_method(p5_thv_vt, "put", thv_put, "k=o,v=o");
  potion_method(p5_thv_vt, "exists", thv_exists, "k=o");
  potion_method(p5_thv_vt, "delete", thv_delete, "k=o");
  potion_method(p5_thv_vt, "p5delete", thv_delete, "k=o");
  potion_method(p5_thv_vt, "keys", thv_keys, 0);
  potion_method(p5_thv_vt, "values", thv_values, 0);
  potion_method(p5_thv_vt, "length", thv_length, 0);
  potion_method(p5_thv_vt, "p5hscalar", thv_scalar, 0);
  potion_method(p5_thv_vt, "string", thv_string, 0);

  potion_method(p5_tsc_vt, "string", p5_tfetch_string, 0);
}
