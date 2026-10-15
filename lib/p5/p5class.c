/**\file lib/p5/p5class.c
  p5 class feature runtime (`use feature 'class'`, see the class rules in
  syn/syntax-p5.y): class names for ref/isa on instances.

  (c) 2026 perl11.com */
#include <string.h>
#include "p5.h"
#include "khash.h"
#include "table.h"

/* the constructor parameter names of the named classes: (class name, names) pairs */
static PN p5_class_names = PN_NIL;

/* p5classname(CLASS, "Name", PARAMS): remember the perl name of a class object, so that
 * `ref $obj` and `$obj isa Name` know it, and its :param names for p5new. Returns the class. */
static PN p5_classname(Potion *P, PN cl, PN self, PN cls, PN name, PN params) {
  if (PN_IS_PTR(cls) && PN_TYPE(cls) == PN_TVTABLE && PN_IS_STR(name)) {
    ((struct PNVtable *)cls)->name = name;
    PN_TOUCH(cls);
    p5_class_names = PN_PUSH(p5_class_names, PN_PUSH(PN_TUP(name), params));
  }
  return cls;
}

static PN p5_class_params(PN name) {
  PN_SIZE i;
  for (i = 0; i < PN_TUPLE_LEN(p5_class_names); i++) {
    PN e = PN_TUPLE_AT(p5_class_names, i);
    if (PN_TUPLE_AT(e, 0) == name || !strcmp(PN_STR_PTR(PN_TUPLE_AT(e, 0)), PN_STR_PTR(name)))
      return PN_TUPLE_AT(e, 1);
  }
  return PN_NIL;
}

/* p5new(CLASS, ARGS): CLASS->new(ARGS) when the argument names are not known at compile
 * time, e.g. Class->new(%args). ARGS is the argument list, hashes and arrays in it are
 * flattened to key, value, ... The pairs are matched to the :param names of the class and
 * the constructor is called with them in positional order, PN_P5NOARG for the missing ones. */
static PN p5_new(Potion *P, PN cl, PN self, PN cls, PN args) {
  PN flat = PN_TUP0(), params, ary = PN_TUP0(), obj, ctor;
  PN_SIZE i, k, np;
  struct PNVtable *vt = (struct PNVtable *)cls;
  vPN(Closure) c;
  if (!PN_IS_PTR(cls) || PN_TYPE(cls) != PN_TVTABLE || !PN_IS_STR(vt->name))
    return potion_send(PN_STR("Can't locate object method \"new\" via a non-class"), PN_STR("die"));
  params = p5_class_params(vt->name);
  np = PN_IS_TUPLE(params) ? PN_TUPLE_LEN(params) : 0;
  for (i = 0; PN_IS_TUPLE(args) && i < PN_TUPLE_LEN(args); i++) {
    PN a = PN_TUPLE_AT(args, i);
    if (PN_IS_TUPLE(a)) {
      for (k = 0; k < PN_TUPLE_LEN(a); k++) flat = PN_PUSH(flat, PN_TUPLE_AT(a, k));
    } else if (PN_IS_PTR(a) && PN_TYPE(a) == PN_TTABLE) {
      PN pairs = p5_table_pairs(P, a);
      for (k = 0; k < PN_TUPLE_LEN(pairs); k++) flat = PN_PUSH(flat, PN_TUPLE_AT(pairs, k));
    } else flat = PN_PUSH(flat, a);
  }
  if (PN_TUPLE_LEN(flat) % 2)
    return potion_send(potion_str_format(P, "Odd number of arguments to %s->new\n", PN_STR_PTR(vt->name)),
                       PN_STR("die"));
  for (k = 0; k < np; k++) ary = PN_PUSH(ary, PN_P5NOARG);
  for (i = 0; i + 1 < PN_TUPLE_LEN(flat); i += 2) {
    PN key = p5_tostr(P, PN_TUPLE_AT(flat, i));
    for (k = 0; k < np; k++)
      if (PN_IS_STR(key) && !strcmp(PN_STR_PTR(PN_TUPLE_AT(params, k)), PN_STR_PTR(key))) break;
    if (k == np)
      return potion_send(potion_str_format(P, "Unrecognised parameters for \"%s\" constructor: %s\n",
                                           PN_STR_PTR(vt->name), PN_IS_STR(key) ? PN_STR_PTR(key) : "?"),
                         PN_STR("die"));
    PN_TUPLE_AT(ary, k) = PN_TUPLE_AT(flat, i + 1);
  }
  obj = potion_object_new(P, PN_NIL, cls);
  ctor = vt->ctor;
  c = PN_CLOSURE(ctor);
  if (potion_closure_is_vm(ctor)) {
    potion_vm(P, c->data[0], obj, ary, c->extra - 1, &c->data[1]);
  } else { /* jit compiled: the argument registers, padded like the call sites do */
    PN argv[13];
    argv[0] = obj;
    for (i = 0; i < 12; i++) argv[i + 1] = i < np ? PN_TUPLE_AT(ary, i) : PN_P5NOARG;
    potion_call(P, ctor, 13, argv);
  }
  return obj;
}

/* ref($obj): the class name of an instance of a named class, else "" */
static PN p5_ref_obj(Potion *P, PN cl, PN self) {
  struct PNVtable *vt;
  if (!PN_IS_PTR(self)) return PN_STR("");
  vt = (struct PNVtable *)PN_VTABLE(PN_TYPE(self));
  return vt && PN_IS_PTR(vt->name) && PN_IS_STR(vt->name) ? vt->name : PN_STR("");
}

/* $obj isa Name: the class of obj, or one of its parents, is Name */
static PN p5_isa(Potion *P, PN cl, PN self, PN name) {
  PNType t;
  if (!PN_IS_PTR(self) || !PN_IS_STR(name)) return PN_FALSE;
  for (t = PN_TYPE(self); t; ) {
    struct PNVtable *vt = (struct PNVtable *)PN_VTABLE(t);
    if (!vt) break;
    if (PN_IS_PTR(vt->name) && PN_IS_STR(vt->name) && PN_STR_LEN(vt->name) == PN_STR_LEN(name) &&
        !memcmp(PN_STR_PTR(vt->name), PN_STR_PTR(name), PN_STR_LEN(name)))
      return PN_TRUE;
    t = vt->parent;
  }
  return PN_FALSE;
}

void p5_class_init(Potion *P) {
  p5_class_names = PN_TUP0();
  potion_gc_root(P, &p5_class_names);
  potion_method(P->lobby, "p5classname", p5_classname, "cls=o,name=S,params=o");
  potion_method(P->lobby, "p5new", p5_new, "cls=o,args=o");
  potion_method(PN_VTABLE(PN_TOBJECT), "ref", p5_ref_obj, 0);
  potion_method(PN_VTABLE(PN_TOBJECT), "p5isa", p5_isa, "name=S");
}
