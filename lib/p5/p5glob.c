/**\file lib/p5/p5glob.c
  perl5 typeglobs: *name = sub {..} / \&f / \@a / \%h / *other, *{"Pkg::$n"} = .., *name{SLOT}.

  A glob is one object per qualified name. Its slots are the sub registry (p5oo.c) for CODE and the
  lobby globals `@name`, `%name`, `$name` and `*name` for ARRAY, HASH, SCALAR and IO. See AGENTS.md
  for the limits (top level variables are locals, arrays are snapshots).

  (c) 2026 perl11.com */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "p5.h"

/* ---- typeglobs: *name is one object with the slots CODE ($p5_subs), SCALAR ($name), ARRAY (@name), HASH (%name)
 * and IO (*name), all lobby globals / the sub registry under the qualified name (no "main::" prefix for main).
 * *a = *b aliases every slot, *a = \@x / \%x / sub {..} / \&f sets that slot, *a = \$x copies the value
 * (a scalar slot is no real alias), `local *a = ..` does not restore. ---- */
static PN p5_glob_vt;
static PN p5_globs; /* qualified name => the glob object */

/* NAME qualified in package PKG ("" or main: plain), "main::x" is "x" */
static PN p5_glob_qname(Potion *P, PN name, PN pkg) {
  const char *n = PN_STR_PTR(name);
  if (!strncmp(n, "main::", 6)) return potion_str(P, n + 6);
  if (!strncmp(n, "::", 2)) return potion_str(P, n + 2);
  if (!strstr(n, "::") && PN_IS_STR(pkg) && PN_STR_LEN(pkg) && strcmp(PN_STR_PTR(pkg), "main")) {
    char buf[256];
    snprintf(buf, sizeof buf, "%s::%s", PN_STR_PTR(pkg), n);
    return potion_str(P, buf);
  }
  return name;
}
static PN p5_glob_obj(Potion *P, PN qn) {
  PN g = potion_table_at(P, PN_NIL, p5_globs, qn);
  if (g == PN_NIL) {
    g = potion_object_new(P, PN_NIL, p5_glob_vt);
    ((struct PNObject *)g)->ivars[0] = qn;
    potion_table_set(P, p5_globs, qn, g);
  }
  return g;
}
static PN p5_glob_new(Potion *P, PN cl, PN self, PN name, PN pkg) { return p5_glob_obj(P, p5_glob_qname(P, p5_tostr(P, name), pkg)); }
static PN p5_glob_string(Potion *P, PN cl, PN self) {
  PN qn = ((struct PNObject *)self)->ivars[0];
  char buf[256];
  snprintf(buf, sizeof buf, "*%s%s", strstr(PN_STR_PTR(qn), "::") ? "" : "main::", PN_STR_PTR(qn));
  return potion_str(P, buf);
}
static PN p5_glob_name(Potion *P, PN cl, PN self) { return ((struct PNObject *)self)->ivars[0]; }
static PN p5_glob_code(Potion *P, PN qn) {
  PN m = p5_sub_get(P, qn);
  if (m == PN_NIL && !strstr(PN_STR_PTR(qn), "::")) m = p5_sub_get(P, PN_STRCAT("main::", PN_STR_PTR(qn)));
  return m;
}
static PN p5_glob_global(Potion *P, const char *sigil, PN qn) {
  char buf[256];
  snprintf(buf, sizeof buf, "%s%s", sigil, PN_STR_PTR(qn));
  return potion_bind(P, P->lobby, potion_str(P, buf)) == PN_NIL ? PN_NIL : potion_send(P->lobby, potion_str(P, buf));
}
static void p5_glob_define(Potion *P, const char *sigil, PN qn, PN v) {
  char buf[256];
  snprintf(buf, sizeof buf, "%s%s", sigil, PN_STR_PTR(qn));
  potion_define_global(P, potion_str(P, buf), v);
}
static void p5_glob_setcode(Potion *P, PN qn, PN cl) {
  p5_sub_set(P, qn, cl);
  if (!strstr(PN_STR_PTR(qn), "::")) p5_sub_set(P, PN_STRCAT("main::", PN_STR_PTR(qn)), cl);
  potion_define_global(P, qn, cl);
}
/* *NAME = VALUE */
static PN p5_globset(Potion *P, PN cl, PN self, PN name, PN pkg, PN v) {
  PN qn = p5_glob_qname(P, p5_tostr(P, name), pkg), g = p5_glob_obj(P, qn);
  PN t = PN_IS_PTR(v) ? (PN)PN_TYPE(v) : 0;
  if (PN_IS_PTR(v) && PN_TYPE(v) == PN_TCLOSURE) p5_glob_setcode(P, qn, v);
  else if (PN_IS_PTR(v) && PN_TYPE(v) == potion_class_type(P, p5_glob_vt)) { /* *a = *b: every slot */
    PN src = ((struct PNObject *)v)->ivars[0], c = p5_glob_code(P, src);
    static const char *sig[] = { "$", "@", "%", "*", 0 };
    int i;
    for (i = 0; sig[i]; i++) {
      PN x = p5_glob_global(P, sig[i], src);
      if (x != PN_NIL) p5_glob_define(P, sig[i], qn, x);
    }
    if (c != PN_NIL) p5_glob_setcode(P, qn, c);
  }
  else if (PN_IS_PTR(v) && PN_TYPE(v) == PN_TTUPLE) p5_glob_define(P, "@", qn, v);
  else if (PN_IS_PTR(v) && PN_TYPE(v) == PN_TTABLE) p5_glob_define(P, "%", qn, v);
  else if (PN_IS_PTR(v) && PN_TYPE(v) == PN_TSTRING) return p5_globset(P, cl, self, v, PN_NIL, p5_glob_obj(P, p5_glob_qname(P, v, PN_NIL)));
  else if (PN_IS_PTR(v)) p5_glob_define(P, "$", qn, potion_send(v, PN_STR("p5deref"))); /* *a = \$x: the value of $x now */
  (void)t;
  return g;
}
/* *NAME{SLOT} / *{$glob}{SLOT} */
static PN p5_globslot(Potion *P, PN cl, PN self, PN name, PN pkg, PN slot) {
  PN qn = (PN_IS_PTR(name) && PN_TYPE(name) == potion_class_type(P, p5_glob_vt)) ? ((struct PNObject *)name)->ivars[0]
          : p5_glob_qname(P, p5_tostr(P, name), pkg);
  const char *sl = PN_STR_PTR(p5_tostr(P, slot));
  if (!strcmp(sl, "CODE")) return p5_glob_code(P, qn);
  if (!strcmp(sl, "ARRAY")) return p5_glob_global(P, "@", qn);
  if (!strcmp(sl, "HASH")) return p5_glob_global(P, "%", qn);
  if (!strcmp(sl, "SCALAR")) return p5_glob_global(P, "$", qn);
  if (!strcmp(sl, "IO")) return p5_glob_global(P, "*", qn);
  if (!strcmp(sl, "NAME")) { const char *c = strrchr(PN_STR_PTR(qn), ':'); return potion_str(P, c ? c + 1 : PN_STR_PTR(qn)); }
  if (!strcmp(sl, "PACKAGE")) {
    const char *c = strrchr(PN_STR_PTR(qn), ':');
    return c && c > PN_STR_PTR(qn) ? potion_str2(P, PN_STR_PTR(qn), c - 1 - PN_STR_PTR(qn)) : potion_str(P, "main");
  }
  return PN_NIL;
}


void p5_glob_init(Potion *P) {
  p5_globs = potion_table_empty(P);   potion_gc_root(P, &p5_globs);
  p5_glob_vt = potion_class(P, 0, 0, PN_TUP(PN_STR("name")));
  potion_gc_root(P, &p5_glob_vt);
  potion_method(p5_glob_vt, "string", p5_glob_string, 0);
  potion_method(p5_glob_vt, "p5globname", p5_glob_name, 0);
  potion_method(P->lobby, "p5glob", p5_glob_new, "name=o,pkg=o");
  potion_method(P->lobby, "p5globset", p5_globset, "name=o,pkg=o,v=o");
  potion_method(P->lobby, "p5globslot", p5_globslot, "name=o,pkg=o,slot=o");
}
