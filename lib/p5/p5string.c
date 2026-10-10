/**\file lib/p5/p5string.c
  p5 string natives: ref, print/say STDERR, substr, index, rindex, join, sprintf.

  (c) 2026 perl11 org */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "p5.h"

/* ref($x): "ARRAY" (tuple), "HASH" (table), "CODE" (closure), "" otherwise.
 * Registered on each of those vtables, so 'ref $x' self-chains. */
static PN p5_ref(Potion *P, PN cl, PN self) {
  if (PN_IS_TUPLE(self)) return PN_STR("ARRAY");
  if (PN_IS_PTR(self) && PN_TYPE(self) == PN_TTABLE) return PN_STR("HASH");
  if (PN_IS_PTR(self) && PN_TYPE(self) == PN_TCLOSURE) return PN_STR("CODE");
  return PN_STR("");
}

/* print STDERR LIST / say STDERR LIST */
static PN p5_str_eprint(Potion *P, PN cl, PN self) {
  fputs(PN_STR_PTR(self), stderr);
  return self;
}
static PN p5_str_eprintln(Potion *P, PN cl, PN self) {
  fputs(PN_STR_PTR(self), stderr);
  fputc('\n', stderr);
  return self;
}

/* substr(str, off [, len]): negative off/len count from the end */
static PN p5_substr(Potion *P, PN cl, PN self, PN str, PN off, PN len) {
  long n, o, l;
  if (!PN_IS_STR(str)) return PN_NIL;
  n = (long)PN_STR_LEN(str);
  o = PN_IS_INT(off) ? PN_INT(off) : 0;
  if (o < 0) o += n;
  if (o < 0) o = 0;
  if (o > n) return PN_NIL;
  l = PN_IS_INT(len) ? PN_INT(len) : n - o;
  if (l < 0) l = n - o + l;
  if (l < 0) l = 0;
  if (o + l > n) l = n - o;
  return potion_str2(P, PN_STR_PTR(str) + o, l);
}
static PN p5_index(Potion *P, PN cl, PN self, PN str, PN sub, PN pos) {
  long n, o;
  const char *f;
  if (!PN_IS_STR(str) || !PN_IS_STR(sub)) return PN_NUM(-1);
  n = (long)PN_STR_LEN(str);
  o = PN_IS_INT(pos) ? PN_INT(pos) : 0;
  if (o < 0) o = 0;
  if (o > n) o = n;
  f = strstr(PN_STR_PTR(str) + o, PN_STR_PTR(sub));
  return PN_NUM(f ? f - PN_STR_PTR(str) : -1);
}
static PN p5_rindex(Potion *P, PN cl, PN self, PN str, PN sub, PN pos) {
  long n, m, o;
  if (!PN_IS_STR(str) || !PN_IS_STR(sub)) return PN_NUM(-1);
  n = (long)PN_STR_LEN(str); m = (long)PN_STR_LEN(sub);
  o = PN_IS_INT(pos) ? PN_INT(pos) : n - m;
  if (o > n - m) o = n - m;
  for (; o >= 0; o--)
    if (!memcmp(PN_STR_PTR(str) + o, PN_STR_PTR(sub), m)) return PN_NUM(o);
  return PN_NUM(-1);
}

typedef struct { char *p; size_t len, cap; } p5buf;
static void p5buf_add(p5buf *b, const char *s, size_t n) {
  if (b->len + n + 1 > b->cap) {
    b->cap = (b->len + n + 1) * 2;
    b->p = realloc(b->p, b->cap);
  }
  memcpy(b->p + b->len, s, n);
  b->len += n;
  b->p[b->len] = 0;
}
static void p5buf_pn(Potion *P, p5buf *b, PN v) {
  PN s = PN_IS_STR(v) ? v : potion_send(v, PN_string);
  if (PN_IS_STR(s)) p5buf_add(b, PN_STR_PTR(s), PN_STR_LEN(s));
}
/* join(sep, a, b, ...): a tuple argument contributes its elements */
static PN p5_join(Potion *P, PN cl, PN self, PN sep, PN a, PN b, PN c, PN d, PN e, PN f) {
  p5buf buf = {0, 0, 0};
  PN args[6] = { a, b, c, d, e, f };
  int i, first = 1;
  PN r;
  if (!PN_IS_STR(sep)) sep = PN_STR("");
  p5buf_add(&buf, "", 0);
  for (i = 0; i < 6; i++) {
    if (args[i] == PN_NIL) continue;
    if (PN_IS_TUPLE(args[i])) {
      PN_SIZE j;
      for (j = 0; j < PN_TUPLE_LEN(args[i]); j++) {
        if (!first) p5buf_add(&buf, PN_STR_PTR(sep), PN_STR_LEN(sep));
        p5buf_pn(P, &buf, PN_TUPLE_AT(args[i], j)); first = 0;
      }
    } else {
      if (!first) p5buf_add(&buf, PN_STR_PTR(sep), PN_STR_LEN(sep));
      p5buf_pn(P, &buf, args[i]); first = 0;
    }
  }
  r = potion_str2(P, buf.p, buf.len); free(buf.p); return r;
}
/* sprintf(fmt, a, b, ...): %d %i %u %s %c %f %g %e %x %X %o %b %% with flags/width/precision */
static PN p5_sprintf(Potion *P, PN cl, PN self, PN fmt, PN a, PN b, PN c, PN d, PN e, PN f) {
  p5buf out = {0, 0, 0};
  PN args[6] = { a, b, c, d, e, f };
  int ai = 0;
  const char *p;
  PN r;
  if (!PN_IS_STR(fmt)) return PN_NIL;
  p5buf_add(&out, "", 0);
  for (p = PN_STR_PTR(fmt); *p; p++) {
    char spec[32], tmp[512];
    int n = 0;
    PN v;
    if (*p != '%') { p5buf_add(&out, p, 1); continue; }
    if (p[1] == '%') { p5buf_add(&out, "%", 1); p++; continue; }
    spec[n++] = *p++;
    while (*p && strchr("-+ #0123456789.", *p) && n < 24) spec[n++] = *p++;
    v = ai < 6 ? args[ai++] : PN_NIL;
    switch (*p) {
    case 'd': case 'i': case 'u':
      spec[n++] = 'l'; spec[n++] = 'd'; spec[n] = 0;
      snprintf(tmp, sizeof tmp, spec, PN_IS_NUM(v) ? (long)PN_DBL(v) : 0L);
      p5buf_add(&out, tmp, strlen(tmp)); break;
    case 'x': case 'X': case 'o':
      spec[n++] = 'l'; spec[n++] = *p; spec[n] = 0;
      snprintf(tmp, sizeof tmp, spec, PN_IS_NUM(v) ? (unsigned long)PN_DBL(v) : 0UL);
      p5buf_add(&out, tmp, strlen(tmp)); break;
    case 'f': case 'g': case 'e': case 'G': case 'E':
      spec[n++] = *p; spec[n] = 0;
      snprintf(tmp, sizeof tmp, spec, PN_IS_NUM(v) ? PN_DBL(v) : 0.0);
      p5buf_add(&out, tmp, strlen(tmp)); break;
    case 'c': {
      char ch = PN_IS_NUM(v) ? (char)(long)PN_DBL(v) : '?';
      p5buf_add(&out, &ch, 1); break;
    }
    case 's': {
      p5buf sb = {0, 0, 0};
      p5buf_add(&sb, "", 0);
      if (v != PN_NIL) p5buf_pn(P, &sb, v);
      spec[n++] = 's'; spec[n] = 0;
      snprintf(tmp, sizeof tmp, spec, sb.p);
      p5buf_add(&out, tmp, strlen(tmp)); free(sb.p); break;
    }
    default: p5buf_add(&out, spec, n); if (*p) p5buf_add(&out, p, 1); break;
    }
    if (!*p) break;
  }
  r = potion_str2(P, out.p, out.len); free(out.p); return r;
}

void p5_string_init(Potion *P) {
  PN str_vt = PN_VTABLE(PN_TSTRING);
  potion_method(str_vt, "eprint", p5_str_eprint, 0);
  potion_method(str_vt, "eprintln", p5_str_eprintln, 0);
  potion_method(str_vt, "ref", p5_ref, 0);
  potion_method(PN_VTABLE(PN_TTUPLE), "ref", p5_ref, 0);
  potion_method(PN_VTABLE(PN_TTABLE), "ref", p5_ref, 0);
  potion_method(PN_VTABLE(PN_TCLOSURE), "ref", p5_ref, 0);
  potion_method(PN_VTABLE(PN_TNUMBER), "ref", p5_ref, 0);
  potion_method(P->lobby, "substr", p5_substr, "str=S,off=N|len=o");
  potion_method(P->lobby, "index", p5_index, "str=S,sub=S|pos=o");
  potion_method(P->lobby, "rindex", p5_rindex, "str=S,sub=S|pos=o");
  potion_method(P->lobby, "join", p5_join, "sep=S|a=o,b=o,c=o,d=o,e=o,f=o");
  potion_method(P->lobby, "sprintf", p5_sprintf, "fmt=S|a=o,b=o,c=o,d=o,e=o,f=o");
}
