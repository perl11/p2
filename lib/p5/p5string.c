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
  PN cls = p5_class_of(P, self); /* blessed: the package name */
  if (cls != PN_NIL) return cls;
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
  str = p5_tostr(P, str); /* substr($num, ..) */
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
  str = p5_tostr(P, str); sub = p5_tostr(P, sub);
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
  str = p5_tostr(P, str); sub = p5_tostr(P, sub);
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
  PN s = p5_tostr(P, v);
  if (PN_IS_STR(s)) p5buf_add(b, PN_STR_PTR(s), PN_STR_LEN(s));
}
/* LIST.p5joins(SEP): join(SEP, LIST); the receiver is the already flattened list (a lone scalar
 * is a one item list); undef items are "" */
static PN p5_joins(Potion *P, PN cl, PN self, PN sep) {
  p5buf buf = {0, 0, 0};
  PN_SIZE i, n;
  PN r;
  sep = p5_tostr(P, sep);
  p5buf_add(&buf, "", 0);
  if (PN_IS_PTR(self) && PN_TYPE(self) == PN_TTABLE) self = p5_table_pairs(P, self);
  if (!PN_IS_TUPLE(self)) self = PN_TUP(self);
  n = PN_TUPLE_LEN(self);
  for (i = 0; i < n; i++) {
    if (i) p5buf_add(&buf, PN_STR_PTR(sep), PN_STR_LEN(sep));
    p5buf_pn(P, &buf, PN_TUPLE_AT(self, i));
    sep = p5_tostr(P, sep); /* the sends above may have moved it */
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

/* print LIST / say LIST to STDOUT: the receiver is the joined item, returns 1 */
static PN p5_lprint(Potion *P, PN cl, PN self) {
  PN str = p5_tostr(P, self);
  if (PN_IS_STR(str)) fwrite(PN_STR_PTR(str), 1, PN_STR_LEN(str), stdout);
  return PN_TRUE;
}
static PN p5_lsay(Potion *P, PN cl, PN self) {
  p5_lprint(P, cl, self);
  fputc('\n', stdout);
  return PN_TRUE;
}

/* split PATTERN, STRING [, LIMIT] (the parser passes "\001" for the awk mode ' ') */
static PN p5_split(Potion *P, PN cl, PN self, PN pat, PN str, PN limit) {
  PN out = PN_TUP0(), all;
  const char *s;
  long len, pos = 0, i, lim = PN_IS_INT(limit) ? PN_INT(limit) : 0, nf = 0;
  int awk = PN_IS_STR(pat) && PN_STR_LEN(pat) == 1 && PN_STR_PTR(pat)[0] == '\001';
  str = p5_tostr(P, str);
  if (!PN_IS_STR(str)) return out;
  s = PN_STR_PTR(str);
  len = PN_STR_LEN(str);
  if (awk) {
    while (pos < len && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' || s[pos] == '\r' || s[pos] == '\f')) pos++;
    pat = PN_STR("\\s+");
  }
  if (len == 0) return out;
  if (PN_IS_STR(pat) && PN_STR_LEN(pat) == 1 && PN_STR_PTR(pat)[0] == '^') pat = PN_STR("(?m)^"); /* split /^/ is /^/m */
  if (pat == PN_TRUE) pat = PN_STR("1"); /* split 'abc' =~ /b/, ...: the match result is the pattern */
  /* at most LIMIT fields: LIMIT-1 separators */
  all = potion_regex_all(P, PN_NIL, str, pat, lim > 0 ? PN_NUM(lim) : PN_NUM(0));
  if (!PN_IS_TUPLE(all)) return all;
  for (i = 0; i < (long)PN_TUPLE_LEN(all); i++) {
    PN rec = PN_TUPLE_AT(all, i);
    long st = PN_INT(PN_TUPLE_AT(rec, 0)), en = PN_INT(PN_TUPLE_AT(rec, 1)), g;
    if (lim > 0 && nf >= lim - 1) break;
    if (st < pos) continue;
    if (en == st && (st == 0 || st >= len || st == pos)) continue; /* an empty match never splits at the ends */
    if (st == 0 && en > 0 && pos == 0) { /* a leading separator gives a leading empty field */ }
    out = PN_PUSH(out, PN_STRN(PN_STR_PTR(str) + pos, st - pos));
    nf++;
    for (g = 2; g < (long)PN_TUPLE_LEN(rec); g++) out = PN_PUSH(out, PN_TUPLE_AT(rec, g));
    pos = en;
  }
  out = PN_PUSH(out, PN_STRN(PN_STR_PTR(str) + pos, len - pos));
  if (lim == 0) { /* trailing empty fields are removed */
    long n = (long)PN_TUPLE_LEN(out);
    PN kept;
    while (n > 0) {
      PN last = PN_TUPLE_AT(out, n - 1);
      if (last != PN_NIL && !(PN_IS_STR(last) && PN_STR_LEN(last) == 0)) break;
      n--;
    }
    kept = potion_tuple_with_size(P, n);
    for (i = 0; i < n; i++) PN_TUPLE_AT(kept, i) = PN_TUPLE_AT(out, i);
    out = kept;
  }
  return out;
}

/* =~ on a number, undef or a reference: match/subst the string value */
static PN p5_any_match(Potion *P, PN cl, PN self, PN pattern) {
  PN str = p5_tostr(P, self);
  return potion_regex_match(P, cl, str, pattern);
}

void p5_string_init(Potion *P) {
  {
    PN rv[5] = { PN_VTABLE(PN_TNUMBER), PN_VTABLE(PN_TINTEGER), PN_VTABLE(PN_TDOUBLE),
                 PN_VTABLE(PN_TNIL), PN_VTABLE(PN_TOBJECT) };
    int i;
    for (i = 0; i < 5; i++) potion_method(rv[i], "match", p5_any_match, "pattern=S");
  }
  potion_method(P->lobby, "split", p5_split, "pat=o,str=o,limit=o");
  potion_method(P->lobby, "p5lprint", p5_lprint, 0);
  potion_method(P->lobby, "p5lsay", p5_lsay, 0);
  PN str_vt = PN_VTABLE(PN_TSTRING);
  potion_method(str_vt, "eprint", p5_str_eprint, 0);
  potion_method(str_vt, "eprintln", p5_str_eprintln, 0);
  potion_method(str_vt, "ref", p5_ref, 0);
  potion_method(PN_VTABLE(PN_TTUPLE), "ref", p5_ref, 0);
  potion_method(PN_VTABLE(PN_TTABLE), "ref", p5_ref, 0);
  potion_method(PN_VTABLE(PN_TCLOSURE), "ref", p5_ref, 0);
  potion_method(PN_VTABLE(PN_TNUMBER), "ref", p5_ref, 0);
  potion_method(P->lobby, "substr", p5_substr, "str=o,off=o|len=o");
  potion_method(P->lobby, "index", p5_index, "str=o,sub=o|pos=o");
  potion_method(P->lobby, "rindex", p5_rindex, "str=o,sub=o|pos=o");
  potion_method(PN_VTABLE(PN_TTUPLE), "p5joins", p5_joins, "sep=o");
  potion_method(PN_VTABLE(PN_TTABLE), "p5joins", p5_joins, "sep=o");
  potion_method(PN_VTABLE(PN_TSTRING), "p5joins", p5_joins, "sep=o");
  potion_method(P->lobby, "sprintf", p5_sprintf, "fmt=S|a=o,b=o,c=o,d=o,e=o,f=o");
}
