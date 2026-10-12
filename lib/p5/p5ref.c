/**\file lib/p5/p5ref.c
  p5 scalar references. Array and hash references are the tuple/table itself
  (so \@a aliases and [..]/{..} are fresh containers); a scalar reference must
  alias the variable: \$x compiles to p5mkref(sub { $x }), the closure captures
  $x in a box (a PNWeakRef, see vm.c OP_PROTO) and the reference object wraps
  that box. $$r reads, $$r = v writes it.

  (c) 2026 perl11 org */
#include <stdio.h>
#include "p5.h"

static PN p5_sref_vt;

/* p5mkref(closure): a SCALAR reference to the first variable the closure captured */
static PN p5_mkref(Potion *P, PN cl, PN self, PN c) {
  PN box = PN_NIL, ref;
  if (PN_IS_PTR(c) && PN_TYPE(c) == PN_TCLOSURE && ((struct PNClosure *)c)->extra >= 2)
    box = ((struct PNClosure *)c)->data[1];
  if (!PN_IS_REF(box)) box = potion_ref(P, PN_NIL); /* not a captured variable: a fresh box */
  ref = potion_object_new(P, PN_NIL, p5_sref_vt);
  ((struct PNObject *)ref)->ivars[0] = box;
  return ref;
}

static PN p5_deref(Potion *P, PN cl, PN self) {
  return PN_DEREF(((struct PNObject *)self)->ivars[0]);
}
static PN p5_assign(Potion *P, PN cl, PN self, PN v) {
  PN box = ((struct PNObject *)self)->ivars[0];
  PN_DEREF(box) = v;
  PN_TOUCH(box);
  return v;
}
static PN p5_sref_ref(Potion *P, PN cl, PN self) {
  PN v = PN_DEREF(((struct PNObject *)self)->ivars[0]);
  PNType t = PN_IS_PTR(v) ? PN_TYPE(v) : 0;
  return (t == PN_TTUPLE || t == PN_TTABLE || t == PN_TCLOSURE || t == potion_class_type(P, p5_sref_vt))
    ? PN_STR("REF") : PN_STR("SCALAR");
}
static PN p5_sref_string(Potion *P, PN cl, PN self) {
  return potion_str_format(P, "SCALAR(0x%lx)", (unsigned long)((struct PNObject *)self)->uniq);
}

/* Not a closure string method: that would change core potion's output for every mode. */
PN p5_tostr(Potion *P, PN v) {
  if (PN_IS_STR(v)) return v;
  if (PN_IS_PTR(v) && PN_TYPE(v) == PN_TCLOSURE)
    return potion_str_format(P, "CODE(0x%lx)", (unsigned long)PN_CLOSURE(v)->data[0]);
  return potion_send(v, PN_string);
}

void p5_ref_init(Potion *P) {
  p5_sref_vt = potion_class(P, 0, 0, PN_TUP(PN_STR("box")));
  potion_gc_root(P, &p5_sref_vt); /* the class object moves with the collector */
  potion_method(P->lobby, "p5mkref", p5_mkref, "c=o");
  potion_method(p5_sref_vt, "p5deref", p5_deref, 0);
  potion_method(p5_sref_vt, "p5assign", p5_assign, "v=o");
  potion_method(p5_sref_vt, "ref", p5_sref_ref, 0);
  potion_method(p5_sref_vt, "string", p5_sref_string, 0);
}
