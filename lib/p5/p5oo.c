/**\file lib/p5/p5oo.c
  perl5 packages as classes: bless, method resolution along @ISA (depth first), can, isa,
  SUPER::, UNIVERSAL::isa.

  A blessed reference is the container itself (the tuple, table or closure): the class name is
  kept in a registry keyed by the object's `uniq`, which the collector copies when it moves the
  object. bless gives the object a fresh unique id, so a class lookup is exact. Named subs of a
  package are registered by name ("Foo::new") by `sub Foo::new {..}` (p5defsub); @ISA by
  p5setisa. `$o->m(args)` is `$o.p5meth("m", (args))`: an object or a package name dispatches
  here, everything else is the plain potion message send (the builtin methods).

  (c) 2026 perl11.com */
#include <stdio.h>
#include <string.h>
#include "p5.h"

static PN p5_blessed; /* uniq => class name */
static PN p5_subs;    /* "Pkg::name" => closure */
static PN p5_isa;     /* "Pkg" => list of parent names */
static PN p5_pkgs;    /* "Pkg" => true */
static PN p5_ovl;     /* "Pkg" => table: operator => code (use overload) */
static PN p5_c3tab;   /* "Pkg" => true: use mro 'c3' */
static unsigned long p5_bless_n = 0x80000000UL;

static PN p5_key2(Potion *P, const char *a, long alen, const char *b, long blen) {
  char buf[256];
  if (alen + blen + 2 >= (long)sizeof buf) return PN_NIL;
  memcpy(buf, a, alen);
  if (b) { memcpy(buf + alen, "::", 2); memcpy(buf + alen + 2, b, blen); alen += blen + 2; }
  return potion_str2(P, buf, alen);
}

/* the class a value is blessed into, or nil */
PN p5_class_of(Potion *P, PN v) {
  if (!PN_IS_PTR(v) || PN_IS_STR(v) || !p5_blessed) return PN_NIL;
  return potion_table_at(P, PN_NIL, p5_blessed,
                         PN_NUM((long)((struct PNObject *)potion_fwd(v))->uniq));
}

/* bless REF, CLASS */
static PN p5_bless(Potion *P, PN cl, PN self, PN ref, PN cls) {
  PN name = cls == PN_NIL ? PN_STR("main") : p5_tostr(P, cls);
  struct PNObject *o;
  if (!PN_IS_PTR(ref) || PN_IS_STR(ref)) return ref;
  o = (struct PNObject *)potion_fwd(ref);
  if (p5_class_of(P, ref) == PN_NIL) o->uniq = (PNUniq)(++p5_bless_n);
  potion_table_set(P, p5_blessed, PN_NUM((long)o->uniq), name);
  return ref;
}

/* package NAME: the name is a class */
static PN p5_pkg(Potion *P, PN cl, PN self, PN name) {
  potion_table_set(P, p5_pkgs, name, PN_TRUE);
  return PN_NIL;
}

/* sub Pkg::name {..}: the registered closure is the value of the definition */
static PN p5_defsub(Potion *P, PN cl, PN self, PN name, PN closure) {
  const char *n = PN_STR_PTR(name), *c = strrchr(n, ':');
  if (c && c > n && c[-1] == ':') /* Pkg::name: Pkg is a class */
    potion_table_set(P, p5_pkgs, potion_str2(P, (char *)n, c - 1 - n), PN_TRUE);
  potion_table_set(P, p5_subs, name, closure);
  return closure;
}

/* @ISA = LIST */
static PN p5_setisa(Potion *P, PN cl, PN self, PN pkg, PN list) {
  PN t = PN_TUP0();
  PN_SIZE i;
  if (PN_IS_TUPLE(list)) for (i = 0; i < PN_TUPLE_LEN(list); i++) t = PN_PUSH(t, p5_tostr(P, PN_TUPLE_AT(list, i)));
  else if (list != PN_NIL) t = PN_PUSH(t, p5_tostr(P, list));
  potion_table_set(P, p5_pkgs, pkg, PN_TRUE);
  potion_table_set(P, p5_isa, pkg, t);
  return t;
}

/* the class names in ITEM (a name or a nested list of them) appended to the list T */
static PN p5_isa_add(Potion *P, PN t, PN item) {
  PN_SIZE i;
  if (PN_IS_TUPLE(item)) {
    for (i = 0; i < PN_TUPLE_LEN(item); i++) t = p5_isa_add(P, t, PN_TUPLE_AT(item, i));
  } else if (PN_IS_STR(item) && PN_STR_LEN(item) > 0 && PN_STR_PTR(item)[0] != '-') t = PN_PUSH(t, item);
  return t;
}
/* push @ISA, ITEM... (also use parent/base LIST: -norequire and other non strings are skipped) */
static PN p5_isapush(Potion *P, PN cl, PN self, PN pkg, PN item) {
  PN t = potion_table_at(P, PN_NIL, p5_isa, pkg);
  if (!PN_IS_TUPLE(t)) t = PN_TUP0();
  t = p5_isa_add(P, t, item);
  potion_table_set(P, p5_pkgs, pkg, PN_TRUE);
  potion_table_set(P, p5_isa, pkg, t);
  return t;
}

#define P5_MAXLIN 64
/* depth first linearization of a class, each class once: out[0] is the class itself */
static int p5_linear(Potion *P, PN cls, PN *out, int n, int depth) {
  PN parents;
  PN_SIZE i;
  int k;
  if (depth > 32 || n >= P5_MAXLIN) return n;
  for (k = 0; k < n; k++) if (out[k] == cls) return n;
  out[n++] = cls;
  parents = potion_table_at(P, PN_NIL, p5_isa, cls);
  if (PN_IS_TUPLE(parents))
    for (i = 0; i < PN_TUPLE_LEN(parents); i++) n = p5_linear(P, PN_TUPLE_AT(parents, i), out, n, depth + 1);
  return n;
}

static PN p5_parents(Potion *P, PN cls, void *ud) {
  return potion_table_at(P, PN_NIL, p5_isa, cls);
}
/* the method resolution order of CLASS: dfs, or c3 for a class that said use mro 'c3' */
static int p5_mro(Potion *P, PN cls, PN *out) {
  if (potion_table_at(P, PN_NIL, p5_c3tab, cls) != PN_NIL) {
    int n = potion_c3_linearize(P, cls, p5_parents, NULL, out);
    if (n >= 0) return n;
  }
  return p5_linear(P, cls, out, 0, 0);
}
/* the method NAME of CLASS or its parents, searching from the SKIP-th class; nil if none */
static PN p5_find(Potion *P, PN cls, PN name, int skip) {
  PN lin[P5_MAXLIN];
  int n = p5_mro(P, cls, lin), k;
  for (k = skip; k < n; k++) {
    PN key = p5_key2(P, PN_STR_PTR(lin[k]), PN_STR_LEN(lin[k]), PN_STR_PTR(name), PN_STR_LEN(name));
    PN m = key == PN_NIL ? PN_NIL : potion_table_at(P, PN_NIL, p5_subs, key);
    if (m != PN_NIL) return m;
  }
  return PN_NIL;
}

static int p5_is_isa(Potion *P, PN cls, PN parent) {
  PN lin[P5_MAXLIN];
  int n = p5_mro(P, cls, lin), k;
  for (k = 0; k < n; k++) if (lin[k] == parent) return 1;
  return 0;
}

static PN p5_die_msg(Potion *P, PN msg) {
  PN die = potion_bind(P, P->lobby, PN_STR("die"));
  if (die != PN_NIL) { PN argv[2] = { P->lobby, msg }; return potion_call(P, die, 2, argv); }
  return PN_NIL;
}

/* call the sub M as a method of INV: INV is the first argument */
static PN p5_invoke(Potion *P, PN m, PN inv, PN args) {
  PN all = PN_TUP(inv);
  PN_SIZE i;
  if (PN_IS_TUPLE(args)) for (i = 0; i < PN_TUPLE_LEN(args); i++) all = PN_PUSH(all, PN_TUPLE_AT(args, i));
  else if (args != PN_NIL) all = PN_PUSH(all, args);
  return potion_send(m, PN_STR("p5callv"), all);
}

/* INV.p5meth(NAME, ARGS): a method call on anything */
static PN p5_meth(Potion *P, PN cl, PN self, PN name, PN args) {
  PN cls, m;
  if (PN_IS_PTR(name) && PN_TYPE(name) == PN_TCLOSURE) return p5_invoke(P, name, self, args); /* $o->$coderef(args) */
  name = p5_tostr(P, name);
  cls = p5_class_of(P, self);
  if (cls == PN_NIL && PN_IS_STR(self) && potion_table_at(P, PN_NIL, p5_pkgs, self) != PN_NIL) cls = self;
  if (cls != PN_NIL) {
    const char *nm = PN_STR_PTR(name);
    m = p5_find(P, cls, name, 0);
    if (m != PN_NIL) return p5_invoke(P, m, self, args);
    if (!strcmp(nm, "can")) {
      PN want = PN_IS_TUPLE(args) ? PN_TUPLE_AT(args, 0) : args, f = want == PN_NIL ? PN_NIL : p5_find(P, cls, p5_tostr(P, want), 0);
      return f != PN_NIL ? f : PN_STR0;
    }
    if (!strcmp(nm, "isa") || !strcmp(nm, "DOES")) {
      PN want = PN_IS_TUPLE(args) ? PN_TUPLE_AT(args, 0) : args;
      return want != PN_NIL && p5_is_isa(P, cls, p5_tostr(P, want)) ? PN_TRUE : PN_STR0;
    }
    if (!strcmp(nm, "import") || !strcmp(nm, "unimport")) return PN_NIL;
    return p5_die_msg(P, potion_str_format(P, "Can't locate object method \"%s\" via package \"%s\"\n", nm, PN_STR_PTR(cls)));
  }
  { /* not a class: the builtin methods of the value (potion message send) */
    PN clo = potion_bind(P, self, name);
    if (clo == PN_NIL && PN_IS_PTR(self) && PN_TYPE(self) == PN_TCLOSURE && !strcmp(PN_STR_PTR(name), "call"))
      return potion_send(self, PN_STR("p5callv"), args); /* $code->call(1, 2) is $code->(1, 2) */
    if (clo == PN_NIL) return PN_NIL;
    return p5_call_method(P, clo, self, args);
  }
}

/* p5super(PKG, INV, NAME, ARGS): $self->SUPER::name(ARGS) written in package PKG */
static PN p5_super(Potion *P, PN cl, PN self, PN pkg, PN inv, PN name, PN args) {
  PN m = p5_find(P, pkg, name, 1);
  if (m == PN_NIL)
    return p5_die_msg(P, potion_str_format(P, "Can't locate object method \"%s\" via package \"%s\"\n",
                                           PN_STR_PTR(name), PN_STR_PTR(pkg)));
  return p5_invoke(P, m, inv, args);
}

/* UNIVERSAL::isa(OBJ, CLASS) / UNIVERSAL::can(OBJ, NAME) / Scalar::Util::blessed(OBJ) */
static const char *p5_reftype(PN v) {
  if (PN_IS_TUPLE(v)) return "ARRAY";
  if (PN_IS_PTR(v) && PN_TYPE(v) == PN_TTABLE) return "HASH";
  if (PN_IS_PTR(v) && PN_TYPE(v) == PN_TCLOSURE) return "CODE";
  return "";
}
static PN p5_u_isa(Potion *P, PN cl, PN self, PN obj, PN parent) {
  PN cls = p5_class_of(P, obj);
  if (cls != PN_NIL && parent != PN_NIL && PN_IS_STR(parent) && !strcmp(PN_STR_PTR(parent), p5_reftype(obj))) return PN_TRUE;
  if (cls == PN_NIL && PN_IS_STR(obj)) cls = obj;
  return cls != PN_NIL && parent != PN_NIL && p5_is_isa(P, cls, p5_tostr(P, parent)) ? PN_TRUE : PN_STR0;
}
static PN p5_u_can(Potion *P, PN cl, PN self, PN obj, PN name) {
  PN cls = p5_class_of(P, obj), m;
  if (cls == PN_NIL && PN_IS_STR(obj)) cls = obj;
  m = cls == PN_NIL || name == PN_NIL ? PN_NIL : p5_find(P, cls, p5_tostr(P, name), 0);
  return m != PN_NIL ? m : PN_STR0;
}
static PN p5_u_blessed(Potion *P, PN cl, PN self, PN obj) { return p5_class_of(P, obj); }

/* the linearized @ISA of a class (mro::get_linear_isa) */
static PN p5_linear_isa(Potion *P, PN cl, PN self, PN cls) {
  PN lin[P5_MAXLIN], out = PN_TUP0();
  int n = p5_mro(P, p5_tostr(P, cls), lin), k;
  for (k = 0; k < n; k++) out = PN_PUSH(out, lin[k]);
  return out;
}

/* use mro 'c3' / 'dfs' in a package; mro::set_mro(PKG, KIND), mro::get_mro(PKG) */
static PN p5_set_mro(Potion *P, PN cl, PN self, PN pkg, PN kind) {
  kind = p5_tostr(P, kind);
  if (!strcmp(PN_STR_PTR(kind), "c3")) potion_table_set(P, p5_c3tab, p5_tostr(P, pkg), PN_TRUE);
  else potion_table_set(P, p5_c3tab, p5_tostr(P, pkg), PN_NIL);
  potion_table_set(P, p5_pkgs, p5_tostr(P, pkg), PN_TRUE);
  return PN_NIL;
}
static PN p5_get_mro(Potion *P, PN cl, PN self, PN pkg) {
  return potion_table_at(P, PN_NIL, p5_c3tab, p5_tostr(P, pkg)) != PN_NIL ? PN_STR("c3") : PN_STR("dfs");
}

/* $self->next::method(ARGS) written in sub NAME of package PKG: the next sub NAME after PKG in the
 * method resolution order of the invocant's class; KIND is "method", "can" or "maybe" */
static PN p5_next(Potion *P, PN cl, PN self, PN pkg, PN subname, PN kind, PN inv, PN args) {
  PN cls = p5_class_of(P, inv), lin[P5_MAXLIN], m = PN_NIL;
  int n, k;
  if (cls == PN_NIL && PN_IS_STR(inv)) cls = inv;
  if (cls == PN_NIL) return PN_NIL;
  n = p5_mro(P, cls, lin);
  for (k = 0; k < n && lin[k] != pkg; k++) ;
  for (k++; k < n && m == PN_NIL; k++) {
    PN key = p5_key2(P, PN_STR_PTR(lin[k]), PN_STR_LEN(lin[k]), PN_STR_PTR(subname), PN_STR_LEN(subname));
    m = key == PN_NIL ? PN_NIL : potion_table_at(P, PN_NIL, p5_subs, key);
  }
  if (!strcmp(PN_STR_PTR(kind), "can")) return m != PN_NIL ? m : PN_STR0;
  if (m == PN_NIL) {
    if (!strcmp(PN_STR_PTR(kind), "maybe")) return PN_NIL;
    return p5_die_msg(P, potion_str_format(P, "No next::method '%s' found for %s\n", PN_STR_PTR(subname), PN_STR_PTR(cls)));
  }
  return p5_invoke(P, m, inv, args);
}

/* use overload LIST: operator => code pairs of a package */
static PN p5_overload(Potion *P, PN cl, PN self, PN pkg, PN list) {
  PN t = potion_table_at(P, PN_NIL, p5_ovl, pkg);
  PN_SIZE i;
  if (t == PN_NIL) { t = potion_table_empty(P); potion_table_set(P, p5_ovl, pkg, t); }
  for (i = 0; PN_IS_TUPLE(list) && i + 1 < PN_TUPLE_LEN(list); i += 2) {
    PN k = PN_TUPLE_AT(list, i), v = PN_TUPLE_AT(list, i + 1);
    if (PN_IS_STR(k) && PN_IS_PTR(v) && PN_TYPE(v) == PN_TCLOSURE) {
      t = potion_table_at(P, PN_NIL, p5_ovl, pkg);
      potion_table_set(P, t, k, v);
    }
  }
  potion_table_set(P, p5_pkgs, pkg, PN_TRUE);
  return PN_NIL;
}

/* the overloading code of OBJ's class for the operator KEY (inherited along the MRO), or nil */
PN p5_overload_find(Potion *P, PN obj, const char *key) {
  PN cls = p5_class_of(P, obj), lin[P5_MAXLIN], k;
  int n, i;
  if (cls == PN_NIL || !p5_ovl) return PN_NIL;
  k = potion_str(P, key);
  n = p5_mro(P, cls, lin);
  for (i = 0; i < n; i++) {
    PN t = potion_table_at(P, PN_NIL, p5_ovl, lin[i]), m;
    if (t == PN_NIL) continue;
    m = potion_table_at(P, PN_NIL, t, k);
    if (m != PN_NIL) return m;
  }
  return PN_NIL;
}
/* "$obj" through an overloaded "", or nil */
PN p5_overload_str(Potion *P, PN obj) {
  PN m = p5_overload_find(P, obj, "\"\"");
  if (m == PN_NIL) return PN_NIL;
  return p5_tostr(P, p5_invoke(P, m, obj, PN_PUSH(PN_PUSH(PN_TUP0(), PN_NIL), PN_STR0)));
}
static PN p5_str_m(Potion *P, PN cl, PN self) { return p5_tostr(P, self); }

void p5_oo_init(Potion *P) {
  p5_ovl = potion_table_empty(P); potion_gc_root(P, &p5_ovl);
  potion_method(P->lobby, "p5overload", p5_overload, "pkg=o,list=o");
  potion_method(P->lobby, "p5str", p5_str_m, 0);
  p5_c3tab = potion_table_empty(P); potion_gc_root(P, &p5_c3tab);
  potion_method(P->lobby, "p5mro", p5_set_mro, "pkg=o,kind=o");
  potion_method(P->lobby, "mro::set_mro", p5_set_mro, "pkg=o,kind=o");
  potion_method(P->lobby, "mro::get_mro", p5_get_mro, "pkg=o");
  potion_method(P->lobby, "p5next", p5_next, "pkg=o,sub=o,kind=o,inv=o,args=o");
  p5_blessed = potion_table_empty(P); potion_gc_root(P, &p5_blessed);
  p5_subs = potion_table_empty(P);    potion_gc_root(P, &p5_subs);
  p5_isa = potion_table_empty(P);     potion_gc_root(P, &p5_isa);
  p5_pkgs = potion_table_empty(P);    potion_gc_root(P, &p5_pkgs);
  potion_method(P->lobby, "bless", p5_bless, "ref=o|cls=o");
  potion_method(P->lobby, "p5pkg", p5_pkg, "name=o");
  potion_method(P->lobby, "p5defsub", p5_defsub, "name=o,cl=o");
  potion_method(P->lobby, "p5setisa", p5_setisa, "pkg=o,list=o");
  potion_method(P->lobby, "p5isapush", p5_isapush, "pkg=o,item=o");
  potion_method(P->lobby, "p5super", p5_super, "pkg=o,inv=o,name=o,args=o");
  potion_method(P->lobby, "UNIVERSAL::isa", p5_u_isa, "obj=o,cls=o");
  potion_method(P->lobby, "UNIVERSAL::can", p5_u_can, "obj=o,name=o");
  potion_method(P->lobby, "Scalar::Util::blessed", p5_u_blessed, "obj=o");
  potion_method(P->lobby, "mro::get_linear_isa", p5_linear_isa, "cls=o");
  potion_method(P->lobby, "p5meth", p5_meth, "name=o,args=o"); /* every value inherits the lobby methods */
}
