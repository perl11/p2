/**\file lib/p5/p5string.c
  p5 string natives: ref, print/say STDERR, substr, index, rindex, join, sprintf.

  (c) 2026 perl11.com */
#include <time.h>
#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>
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
  if (m == 0) { /* empty matches everywhere: the clamped position (perl) */
    if (o < 0) o = 0;
    if (o > n) o = n;
    return PN_NUM(o);
  }
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
  if (PN_TUPLE_LEN(self) == 1 && PN_IS_PTR(PN_TUPLE_AT(self, 0)) && PN_TYPE(PN_TUPLE_AT(self, 0)) == PN_TTABLE) self = p5_table_pairs(P, PN_TUPLE_AT(self, 0)); /* join($s, %$h) */
  n = PN_TUPLE_LEN(self);
  for (i = 0; i < n; i++) {
    if (i) p5buf_add(&buf, PN_STR_PTR(sep), PN_STR_LEN(sep));
    p5buf_pn(P, &buf, PN_TUPLE_AT(self, i));
    sep = p5_tostr(P, sep); /* the sends above may have moved it */
  }
  r = potion_str2(P, buf.p, buf.len); free(buf.p); return r;
}
/* the integer argument of %d %u %x %o: exact for integers and bigints (the 64 bit two's complement
 * for IV/UV), else the truncated double */
static unsigned long long p5_fmt_uv(PN v) {
  uint64_t u;
  if (PN_IS_INT(v)) return (unsigned long long)(long long)PN_INT(v);
  if (PN_IS_BIG(v)) return potion_big_to_u64(v, &u) ? (unsigned long long)u : ~0ULL;
  if (PN_IS_NUM(v)) {
    double d = PN_DBL(v);
    if (d >= 18446744073709551615.0) return ~0ULL;
    if (d >= 9223372036854775808.0) return (unsigned long long)d;
    return (unsigned long long)(long long)d;
  }
  return 0;
}
/* a string argument of a numeric conversion: its numeric prefix, an integer when it has no fraction or exponent */
static int p5_fmt_str_num(PN v, long long *iv, double *dv) {
  const char *s = PN_STR_PTR(v), *h = s;
  char *e;
  while (*h == ' ' || *h == '\t' || *h == '\n' || *h == '\r' || *h == '\f') h++;
  *dv = strtod(h, &e);
  if (e == h) { *iv = 0; *dv = 0; return 1; }
  if (!memchr(h, '.', e - h) && !memchr(h, 'e', e - h) && !memchr(h, 'E', e - h) && !(e - h > 2 && (h[1] == 'x' || h[1] == 'X' || h[2] == 'x' || h[2] == 'X'))) {
    *iv = strtoll(h, NULL, 10);
    return 1;
  }
  *iv = (long long)*dv;
  return 0;
}
static double p5_fmt_dbl(Potion *P, PN v) {
  long long iv; double d;
  if (PN_IS_DBL(v)) return PN_DBL(v);
  if (PN_IS_INT(v)) return (double)PN_INT(v);
  if (PN_IS_BIG(v)) return (double)(long long)p5_fmt_uv(v);
  if (PN_IS_STR(v)) { p5_fmt_str_num(v, &iv, &d); return d; }
  return PN_IS_NUM(v) ? PN_DBL(v) : 0.0;
}
/* the integer of %d %u %x %o %b: a number as p5_fmt_uv, a string by its numeric prefix */
static unsigned long long p5_fmt_int(PN v) {
  long long iv; double d;
  if (PN_IS_STR(v)) {
    if (p5_fmt_str_num(v, &iv, &d)) return (unsigned long long)iv;
    if (d >= 9223372036854775808.0) return d >= 18446744073709551615.0 ? ~0ULL : (unsigned long long)d;
    return (unsigned long long)(long long)d;
  }
  return p5_fmt_uv(v);
}
/* one printf conversion through the C library into the output buffer (grows to fit, no fixed size) */
static void p5_fmt_c(p5buf *out, const char *spec, ...) {
  va_list ap, ap2;
  int n;
  char *tmp;
  va_start(ap, spec);
  va_copy(ap2, ap);
  n = vsnprintf(NULL, 0, spec, ap);
  va_end(ap);
  if (n >= 0 && (tmp = malloc((size_t)n + 1))) {
    vsnprintf(tmp, (size_t)n + 1, spec, ap2);
    p5buf_add(out, tmp, (size_t)n);
    free(tmp);
  }
  va_end(ap2);
}
/* PREFIX + BODY padded to WIDTH: left justified, zero filled between prefix and body, or right justified */
static void p5_fmt_pad(p5buf *out, const char *prefix, const char *body, size_t blen, long width, int left, int zero) {
  size_t plen = strlen(prefix), total = plen + blen;
  long fill = width > 0 && (size_t)width > total ? width - (long)total : 0;
  if (!left && !zero) while (fill-- > 0) p5buf_add(out, " ", 1);
  p5buf_add(out, prefix, plen);
  if (!left && zero) while (fill-- > 0) p5buf_add(out, "0", 1);
  p5buf_add(out, body, blen);
  if (left) while (fill-- > 0) p5buf_add(out, " ", 1);
}
/* sprintf(fmt, a, b, ...): %% %c %s %d %i %u %o %x %X %e %E %f %F %g %G %b %B, flags - + space # 0, vector flag,
 * width / precision (also *), explicit argument index %N$, size letters ignored */
static PN p5_sprintf(Potion *P, PN cl, PN self, PN fmt, PN a, PN b, PN c, PN d, PN e, PN f) {
  p5buf out = {0, 0, 0};
  PN fixed[6] = { a, b, c, d, e, f }, *args = fixed;
  PN_SIZE nargs = 6, k;
  long ai = 0;
  const char *p;
  PN r;
  if (PN_IS_TUPLE(a) && b == PN_NIL && c == PN_NIL) { /* sprintf FMT, @list: any number of arguments */
    nargs = PN_TUPLE_LEN(a);
    args = (PN *)malloc((nargs ? nargs : 1) * sizeof(PN));
    if (!args) potion_allocation_error();
    for (k = 0; k < nargs; k++) args[k] = PN_TUPLE_AT(a, k);
  }
  if (!PN_IS_STR(fmt)) { if (args != fixed) free((void *)args); return PN_NIL; }
  p5buf_add(&out, "", 0);
#define P5_NEXTARG() (ai < (long)nargs ? args[ai++] : (ai++, PN_NIL))
  for (p = PN_STR_PTR(fmt); *p; ) {
    const char *dir = p;
    int left = 0, zero = 0, plus = 0, space = 0, alt = 0, vec = 0, hasprec = 0;
    long width = 0, prec = 0, idx = 0;
    PN v;
    if (*p != '%') { const char *q = p; while (*q && *q != '%') q++; p5buf_add(&out, p, (size_t)(q - p)); p = q; continue; }
    p++;
    if (*p == '%') { p5buf_add(&out, "%", 1); p++; continue; }
    { const char *q = p; long n = 0; /* %N$ : explicit argument index */
      while (isdigit((unsigned char)*q)) n = n * 10 + (*q++ - '0');
      if (q > p && *q == '$' && n > 0) { idx = n; p = q + 1; } }
    for (;; p++) {
      if (*p == '-') left = 1;
      else if (*p == '+') plus = 1;
      else if (*p == ' ') space = 1;
      else if (*p == '#') alt = 1;
      else if (*p == '0') zero = 1;
      else break;
    }
    if (*p == 'v') { vec = 1; p++; }
    if (*p == '*') { long w = (long)p5_fmt_int(P5_NEXTARG()); p++; if (w < 0) { left = 1; w = -w; } width = w; }
    else while (isdigit((unsigned char)*p)) width = width * 10 + (*p++ - '0');
    if (*p == '.') {
      p++; hasprec = 1;
      if (*p == '*') { long pr = (long)p5_fmt_int(P5_NEXTARG()); p++; if (pr < 0) hasprec = 0; else prec = pr; }
      else while (isdigit((unsigned char)*p)) prec = prec * 10 + (*p++ - '0');
    }
    while (*p && strchr("hlqLVzt", *p)) p++;
    if (!*p) { p5buf_add(&out, dir, (size_t)(p - dir)); break; }
    v = idx > 0 ? (idx <= (long)nargs ? args[idx - 1] : PN_NIL) : (strchr("csdiuoxXeEfFgGbB", *p) ? P5_NEXTARG() : PN_NIL);
    switch (*p) {
    case 'c': {
      unsigned long long cp = p5_fmt_int(v);
      char ch[8]; int cn = 0;
      if (cp < 256) ch[cn++] = (char)cp;
      else if (cp < 0x800) { ch[cn++] = (char)(0xC0 | (cp >> 6)); ch[cn++] = (char)(0x80 | (cp & 0x3F)); }
      else if (cp < 0x10000) { ch[cn++] = (char)(0xE0 | (cp >> 12)); ch[cn++] = (char)(0x80 | ((cp >> 6) & 0x3F)); ch[cn++] = (char)(0x80 | (cp & 0x3F)); }
      else { ch[cn++] = (char)(0xF0 | ((cp >> 18) & 7)); ch[cn++] = (char)(0x80 | ((cp >> 12) & 0x3F)); ch[cn++] = (char)(0x80 | ((cp >> 6) & 0x3F)); ch[cn++] = (char)(0x80 | (cp & 0x3F)); }
      p5_fmt_pad(&out, "", ch, (size_t)cn, width, left, 0);
      break;
    }
    case 's': {
      p5buf sb = {0, 0, 0};
      size_t n;
      p5buf_add(&sb, "", 0);
      if (v != PN_NIL) p5buf_pn(P, &sb, v);
      n = sb.len;
      if (hasprec && (size_t)prec < n) n = (size_t)prec;
      p5_fmt_pad(&out, "", sb.p, n, width, left, zero && !left);
      free(sb.p);
      break;
    }
    case 'd': case 'i': case 'u': case 'o': case 'x': case 'X': case 'b': case 'B': {
      int conv = *p;
      PN_SIZE ni = 1, ki;
      PN vs = PN_NIL;
      const char *vp = NULL; size_t vlen = 0, vi = 0;
      if (vec) { vs = p5_tostr(P, v); if (PN_IS_STR(vs)) { vp = PN_STR_PTR(vs); vlen = PN_STR_LEN(vs); } ni = vlen ? vlen : 1; }
      for (ki = 0; ki < ni; ki++) {
        unsigned long long u = vec ? (vlen ? (unsigned char)vp[vi++] : 0) : p5_fmt_int(v);
        char digits[80], prefix[4]; int dn = 0, pn = 0;
        int neg = 0;
        if (vec && ki) p5buf_add(&out, ".", 1);
        if (conv == 'd' || conv == 'i') {
          long long sv = (long long)u;
          neg = sv < 0;
          u = neg ? (unsigned long long)0 - u : u;
          if (neg) prefix[pn++] = '-'; else if (plus) prefix[pn++] = '+'; else if (space) prefix[pn++] = ' ';
          dn = snprintf(digits, sizeof digits, "%llu", u);
        } else if (conv == 'u') dn = snprintf(digits, sizeof digits, "%llu", u);
        else if (conv == 'o') { dn = snprintf(digits, sizeof digits, "%llo", u); if (alt && digits[0] != '0') { memmove(digits + 1, digits, (size_t)dn + 1); digits[0] = '0'; dn++; } }
        else if (conv == 'x' || conv == 'X') {
          dn = snprintf(digits, sizeof digits, conv == 'x' ? "%llx" : "%llX", u);
          if (alt && u) { prefix[pn++] = '0'; prefix[pn++] = conv; }
        } else { /* b B */
          int bit;
          for (bit = 63; bit > 0 && !((u >> bit) & 1); bit--) ;
          for (; bit >= 0; bit--) digits[dn++] = ((u >> bit) & 1) ? '1' : '0';
          digits[dn] = 0;
          if (alt && u) { prefix[pn++] = '0'; prefix[pn++] = conv; }
        }
        prefix[pn] = 0;
        if (hasprec && prec == 0 && u == 0) dn = 0; /* %.0d of 0 is empty */
        if (hasprec && (size_t)prec > (size_t)dn) { /* precision: minimum number of digits, no zero flag */
          char *z = malloc((size_t)prec + 1);
          if (z) { memset(z, '0', (size_t)prec - (size_t)dn); memcpy(z + prec - dn, digits, (size_t)dn); p5_fmt_pad(&out, prefix, z, (size_t)prec, width, left, 0); free(z); }
        } else p5_fmt_pad(&out, prefix, digits, (size_t)dn, width, left, zero && !hasprec);
      }
      break;
    }
    case 'e': case 'E': case 'f': case 'F': case 'g': case 'G': {
      char spec[40]; int sn = 0;
      spec[sn++] = '%';
      if (left) spec[sn++] = '-';
      if (plus) spec[sn++] = '+';
      if (space) spec[sn++] = ' ';
      if (alt) spec[sn++] = '#';
      if (zero) spec[sn++] = '0';
      sn += snprintf(spec + sn, sizeof spec - (size_t)sn, "%ld", width);
      if (hasprec) sn += snprintf(spec + sn, sizeof spec - (size_t)sn, ".%ld", prec);
      spec[sn++] = *p; spec[sn] = 0;
      p5_fmt_c(&out, spec, p5_fmt_dbl(P, v));
      break;
    }
    default: p5buf_add(&out, dir, (size_t)(p - dir) + 1); break; /* an unknown conversion is kept as written */
    }
    p++;
  }
#undef P5_NEXTARG
  r = potion_str2(P, out.p, out.len); free(out.p);
  if (args != fixed) free((void *)args);
  return r;
}

/* print LIST / say LIST to STDOUT: the receiver is the joined item, returns 1 */
static void p5_out(Potion *P, const char *s, long n) {
  if (!p5_sel_out(P, s, n)) fwrite(s, 1, n, stdout);
}
static PN p5_lwrite(Potion *P, PN self) {
  PN str = p5_tostr(P, self);
  if (PN_IS_STR(str)) p5_out(P, PN_STR_PTR(str), PN_STR_LEN(str));
  return PN_TRUE;
}
static PN p5_lprint(Potion *P, PN cl, PN self) {
  PN ors;
  p5_lwrite(P, self);
  ors = potion_send(P->lobby, potion_str(P, "$\\")); /* $\ : the output record separator */
  if (PN_IS_STR(ors) && PN_STR_LEN(ors)) p5_out(P, PN_STR_PTR(ors), PN_STR_LEN(ors));
  return PN_TRUE;
}
static PN p5_lsay(Potion *P, PN cl, PN self) {
  p5_lwrite(P, self);
  p5_out(P, "\n", 1);
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
/* SUBJECT.p5match(PATTERN): the scalar context of m// */
/* m?pat? (PMf_ONCE): matches once until reset; the parser marks the pattern
 * with a "\001<id>;" prefix, unique per textual occurrence (perl's once state
 * is per op, not per pattern text). The fired states live in a GC-rooted
 * table keyed by the full marked text. */
static PN p5_fired;
/* strip the marker; *id_end (when non-nil) is set to the marked text up to and
 * including the ';' (the per-op key), and *pat_start to the pattern proper */
static PN p5_once_strip(Potion *P, PN pattern, char **pat_start) {
  if (PN_IS_STR(pattern) && PN_STR_LEN(pattern) > 1 && PN_STR_PTR(pattern)[0] == '\001') {
    const char *s = PN_STR_PTR(pattern);
    long i, n = PN_STR_LEN(pattern);
    for (i = 1; i < n && s[i] != ';'; i++)
      ;
    if (i < n && i > 1) {
      *pat_start = (char *)s + i + 1;
      return pattern;
    }
  }
  *pat_start = PN_IS_STR(pattern) ? PN_STR_PTR(pattern) : NULL;
  return pattern;
}
static int p5_once_marked(PN pattern) {
  return PN_IS_STR(pattern) && PN_STR_LEN(pattern) > 1 && PN_STR_PTR(pattern)[0] == '\001';
}
/* 1 = the match may proceed (the op is not fired yet); *real is the pattern
 * without the marker */
static int p5_once_gate(Potion *P, PN pattern, PN *real) {
  char *pat;
  p5_once_strip(P, pattern, &pat);
  *real = pat ? PN_STRN(pat, PN_STR_PTR(pattern) + PN_STR_LEN(pattern) - pat) : pattern;
  if (!p5_once_marked(pattern)) return 1;
  if (!PN_IS_PTR(p5_fired)) { p5_fired = potion_table_empty(P); potion_gc_root(P, &p5_fired); }
  return !PN_TEST(potion_table_at(P, PN_NIL, p5_fired, pattern));
}
/* record a successful m?pat? match so the op does not match again */
static void p5_once_mark(Potion *P, PN pattern) {
  if (p5_once_marked(pattern)) potion_table_put(P, PN_NIL, p5_fired, pattern, PN_TRUE);
}
/* reset / reset EXPR: clear the m?pat? one-time match states (all, or those whose
 * pattern begins with the argument) */
static PN p5_reset(Potion *P, PN cl, PN self, PN arg) {
  if (!PN_IS_PTR(p5_fired)) return PN_NIL;
  if (arg == PN_NIL) {
    p5_fired = potion_table_empty(P);
  } else {
    PN s = p5_tostr(P, arg), ks = potion_send(p5_fired, potion_str(P, "keys")), kv;
    PN_SIZE i, n = PN_IS_TUPLE(ks) ? PN_TUPLE_LEN(ks) : 0;
    for (i = 0; i < n; i++) {
      const char *pp;
      kv = PN_TUPLE_AT(ks, i);
      p5_once_strip(P, kv, (char **)&pp);
      if (pp && PN_STR_LEN(s) <= (long)strlen(pp) && !memcmp(pp, PN_STR_PTR(s), PN_STR_LEN(s)))
        potion_table_put(P, PN_NIL, p5_fired, kv, PN_NIL);
    }
  }
  return PN_NIL;
}
static PN p5_match_m(Potion *P, PN cl, PN self, PN pattern) {
  PN real, r;
  if (!p5_once_gate(P, pattern, &real)) return PN_FALSE;
  r = potion_regex_match(P, cl, p5_tostr(P, self), real);
  if (PN_TEST(r)) p5_once_mark(P, pattern);
  return r;
}
/* SUBJECT.p5matchl(PATTERN): m// in list context: the groups, (1) without groups, () on failure */
static PN p5_matchl(Potion *P, PN cl, PN self, PN pattern) {
  PN real, subject, rec, out = PN_TUP0();
  PN_SIZE i;
  if (!p5_once_gate(P, pattern, &real)) return out;
  subject = p5_tostr(P, self);
  rec = potion_send(subject, potion_str(P, "match_at"), real, PN_NUM(0));
  if (!PN_IS_TUPLE(rec)) return out;
  if (PN_TUPLE_LEN(rec) <= 2) { p5_once_mark(P, pattern); return PN_PUSH(out, PN_NUM(1)); }
  for (i = 2; i < PN_TUPLE_LEN(rec); i++) out = PN_PUSH(out, PN_TUPLE_AT(rec, i));
  p5_once_mark(P, pattern);
  return out;
}
static PN p5_any_match(Potion *P, PN cl, PN self, PN pattern) {
  PN str = p5_tostr(P, self);
  return potion_regex_match(P, cl, str, pattern);
}

/* chomp: the string without one trailing newline; undef and non-strings are unchanged */
static PN p5_chomp(Potion *P, PN cl, PN self) {
  PN rs = potion_send(P->lobby, PN_STR("$/"));
  long n = PN_STR_LEN(self), rl = 0, k;
  /* no raw PN_STR_PTR across an allocation (the defines and PN_STRN below): the collector moves strings */
  if (PN_IS_STR(rs)) rl = PN_STR_LEN(rs);
  else if (rs != PN_NIL) { potion_define_global(P, PN_STR("$^CHOMPED"), PN_ZERO); return self; } /* $/ = \N: record reads, nothing to chomp */
  if (rs == PN_NIL) { potion_define_global(P, PN_STR("$^CHOMPED"), PN_ZERO); return self; } /* undef $/: nothing is removed */
  if (rl == 0) { /* paragraph mode: all trailing newlines */
    k = n;
    while (k > 0 && PN_STR_PTR(self)[k - 1] == '\n') k--;
  } else if (n >= rl && !memcmp(PN_STR_PTR(self) + n - rl, PN_STR_PTR(rs), rl)) k = n - rl;
  else k = n;
  if (k == n) { potion_define_global(P, PN_STR("$^CHOMPED"), PN_ZERO); return self; }
  potion_define_global(P, PN_STR("$^CHOMPED"), PN_NUM(n - k));
  return PN_STRN(PN_STR_PTR(self), k);
}
/* substr($s, OFF, LEN) = R: the string with that part replaced (LEN nil: to the end) */
static PN p5_substrset(Potion *P, PN cl, PN self, PN off, PN len, PN repl) {
  long n = PN_STR_LEN(self), o = PN_IS_INT(off) ? PN_INT(off) : 0, l;
  PN r = p5_tostr(P, repl), out;
  char *buf;
  if (o < 0) o += n;
  if (o < 0) o = 0;
  if (o > n) o = n;
  l = len == PN_NIL ? n - o : PN_IS_INT(len) ? PN_INT(len) : 0;
  if (l < 0) l = n - o + l;
  if (l < 0) l = 0;
  if (l > n - o) l = n - o;
  buf = (char *)malloc(n - l + PN_STR_LEN(r) + 1);
  if (!buf) potion_allocation_error();
  memcpy(buf, PN_STR_PTR(self), o);
  memcpy(buf + o, PN_STR_PTR(r), PN_STR_LEN(r));
  memcpy(buf + o + PN_STR_LEN(r), PN_STR_PTR(self) + o + l, n - o - l);
  out = PN_STRN(buf, n - l + PN_STR_LEN(r));
  free(buf);
  return out;
}
/* chop: the string without its last character (UTF-8 aware) */
static PN p5_chop(Potion *P, PN cl, PN self) {
  long n = PN_STR_LEN(self);
  PN removed, rest;
  if (n <= 0) { potion_define_global(P, PN_STR("$^CHOPPED"), PN_STR0); return self; }
  n--;
  while (n > 0 && (((unsigned char)PN_STR_PTR(self)[n]) & 0xC0) == 0x80) n--;
  removed = PN_STRN(PN_STR_PTR(self) + n, PN_STR_LEN(self) - n); /* the value of chop */
  potion_define_global(P, PN_STR("$^CHOPPED"), removed);
  rest = PN_STRN(PN_STR_PTR(self), n);
  return rest;
}
/* chomp(@a): every string element chomped */
static PN p5_chomp_list(Potion *P, PN cl, PN self) {
  PN out = PN_TUP0();
  PN_SIZE i;
  for (i = 0; i < PN_TUPLE_LEN(self); i++) {
    PN e = PN_TUPLE_AT(self, i);
    out = PN_PUSH(out, PN_IS_STR(e) ? p5_chomp(P, cl, e) : e);
  }
  return out;
}
/* chop(@a): every string element chopped */
static PN p5_chop_list(Potion *P, PN cl, PN self) {
  PN out = PN_TUP0();
  PN_SIZE i;
  for (i = 0; i < PN_TUPLE_LEN(self); i++) {
    PN e = PN_TUPLE_AT(self, i);
    out = PN_PUSH(out, PN_IS_STR(e) ? p5_chop(P, cl, e) : e);
  }
  return out;
}
static PN p5_chomp_id(Potion *P, PN cl, PN self) { return self; }


/* ---- m//g: scalar context walks the matches, the position is kept per variable name ---- */
static PN p5_pos_tab;
static PN p5_postab(Potion *P) {
  if (!p5_pos_tab) { p5_pos_tab = potion_table_empty(P); potion_gc_root(P, &p5_pos_tab); }
  return p5_pos_tab;
}
/* the byte offset after the last /g match of the variable KEY (a string "$s") on SUBJECT, -1 if none */
static long p5_pos_of(Potion *P, PN key, PN subject) {
  PN e = potion_table_at(P, PN_NIL, p5_postab(P), key);
  if (!PN_IS_TUPLE(e) || !PN_IS_STR(PN_TUPLE_AT(e, 0))) return -1;
  if (PN_STR_LEN(PN_TUPLE_AT(e, 0)) != PN_STR_LEN(subject) ||
      memcmp(PN_STR_PTR(PN_TUPLE_AT(e, 0)), PN_STR_PTR(subject), PN_STR_LEN(subject))) return -1; /* assigned a new value */
  return PN_INT(PN_TUPLE_AT(e, 1));
}
/* SUBJECT.p5matchg(PATTERN, KEY): the next match after pos(KEY): true and the new pos, else false and pos reset */
static PN p5_matchg(Potion *P, PN cl, PN self, PN pattern, PN key, PN keep) {
  PN subject = p5_tostr(P, self), rec, e;
  long off = p5_pos_of(P, key, subject), start, end;
  int again = 0;
  PN tab = p5_postab(P);
  if (off < 0) off = 0;
  else {
    PN old = potion_table_at(P, PN_NIL, tab, key);
    again = PN_TUPLE_LEN(old) > 2 && PN_TUPLE_AT(old, 2) == PN_TRUE; /* the last match was empty */
  }
  rec = potion_send(subject, potion_str(P, "match_at"), pattern, PN_NUM(off));
  if (PN_IS_TUPLE(rec) && again && PN_INT(PN_TUPLE_AT(rec, 0)) == off && PN_INT(PN_TUPLE_AT(rec, 1)) == off) {
    /* an empty match at the old position is not taken twice: look one character further */
    long o2 = off + 1;
    while (o2 < (long)PN_STR_LEN(subject) && (((unsigned char)PN_STR_PTR(subject)[o2]) & 0xC0) == 0x80) o2++;
    rec = o2 > (long)PN_STR_LEN(subject) ? PN_NIL : potion_send(subject, potion_str(P, "match_at"), pattern, PN_NUM(o2));
  }
  if (!PN_IS_TUPLE(rec)) {
    if (keep != PN_TRUE) potion_table_put(P, PN_NIL, tab, key, PN_NIL); /* m//gc keeps pos */
    return PN_FALSE;
  }
  start = PN_INT(PN_TUPLE_AT(rec, 0)); end = PN_INT(PN_TUPLE_AT(rec, 1));
  e = PN_PUSH(PN_PUSH(PN_PUSH(PN_TUP0(), subject), PN_NUM(end)), end == start ? PN_TRUE : PN_FALSE);
  potion_table_put(P, PN_NIL, tab, key, e);
  return PN_TRUE;
}
/* pos($s): the character position after the last /g match, undef if none (KEY is "$s") */
static PN p5_pos(Potion *P, PN cl, PN self, PN key, PN subject) {
  PN s = p5_tostr(P, subject);
  long off = p5_pos_of(P, key, s), n = 0, i;
  if (off < 0) return PN_NIL;
  for (i = 0; i < off; i++) if ((((unsigned char)PN_STR_PTR(s)[i]) & 0xC0) != 0x80) n++;
  return PN_NUM(n);
}
/* pos($s) = N: the next //g match starts after character N (negative: from the end; clamped), undef resets it */
static PN p5_posset(Potion *P, PN cl, PN self, PN key, PN subject, PN val) {
  PN s = p5_tostr(P, subject), tab = p5_postab(P);
  long n, chars = 0, i, off = 0;
  if (val == PN_NIL) { potion_table_put(P, PN_NIL, tab, key, PN_NIL); return PN_NIL; }
  n = PN_IS_INT(val) ? PN_INT(val) : PN_IS_DBL(val) ? (long)PN_DBL(val) : 0;
  for (i = 0; i < (long)PN_STR_LEN(s); i++) if ((((unsigned char)PN_STR_PTR(s)[i]) & 0xC0) != 0x80) chars++;
  if (n < 0) n += chars;
  if (n < 0) n = 0;
  if (n > chars) n = chars;
  for (i = 0; i < (long)PN_STR_LEN(s) && n > 0; i++)
    if (i + 1 >= (long)PN_STR_LEN(s) || (((unsigned char)PN_STR_PTR(s)[i + 1]) & 0xC0) != 0x80) { n--; off = i + 1; }
  potion_table_put(P, PN_NIL, tab, key, PN_PUSH(PN_PUSH(PN_PUSH(PN_TUP0(), s), PN_NUM(off)), PN_FALSE));
  return PN_NUM(off == 0 ? 0 : PN_INT(val) < 0 ? chars + PN_INT(val) : PN_INT(val) > chars ? chars : PN_INT(val));
}
/* SUBJECT.p5matchall(PATTERN): m//g in list context: the groups of every match, or the matches
 * when the pattern has no groups */
static PN p5_matchall(Potion *P, PN cl, PN self, PN pattern) {
  PN subject = p5_tostr(P, self), all = potion_regex_all(P, PN_NIL, subject, pattern, PN_NUM(0)), out = PN_TUP0();
  PN_SIZE i, j;
  if (!PN_IS_TUPLE(all)) return out;
  for (i = 0; i < PN_TUPLE_LEN(all); i++) {
    PN rec = PN_TUPLE_AT(all, i);
    if (PN_TUPLE_LEN(rec) <= 2) {
      long st = PN_INT(PN_TUPLE_AT(rec, 0)), en = PN_INT(PN_TUPLE_AT(rec, 1));
      out = PN_PUSH(out, potion_str2(P, PN_STR_PTR(subject) + st, en - st));
    } else {
      for (j = 2; j < PN_TUPLE_LEN(rec); j++) out = PN_PUSH(out, PN_TUPLE_AT(rec, j));
    }
  }
  return out;
}

/* ---- s/// and tr/// ---- */
/* STR.p5subst(PATTERN, REPLACEMENT, GLOBAL): the new string; p5subcount is the number of substitutions */
static PN p5_subst_m(Potion *P, PN cl, PN self, PN pattern, PN repl, PN global) {
  PN str = p5_tostr(P, self);
  PN r = potion_send(str, potion_str(P, "subst"), pattern, repl, global);
  return r;
}
/* STR.p5substf(PATTERN, CLOSURE, GLOBAL): s/// whose replacement is computed for every match by
 * CLOSURE (it sees $1, $& of that match); p5subcount is the number of substitutions */
static PN p5_substf(Potion *P, PN cl, PN self, PN pattern, PN closure, PN global) {
  PN subject = p5_tostr(P, self), out = PN_NIL;
  long len = PN_STR_LEN(subject), pos = 0, last = 0, count = 0;
  char *src = malloc(len + 1), *buf = NULL;
  size_t blen = 0, bcap = 0;
  PN result;
  if (!src) potion_allocation_error();
  memcpy(src, PN_STR_PTR(subject), len);
#define P5_APPEND(p, n) do { size_t nn_ = (n); if (blen + nn_ + 1 > bcap) { bcap = (blen + nn_ + 1) * 2; buf = realloc(buf, bcap); if (!buf) potion_allocation_error(); } memcpy(buf + blen, (p), nn_); blen += nn_; } while (0)
  while (pos <= len) {
    PN rec = potion_send(subject, potion_str(P, "match_at"), pattern, PN_NUM(pos)), r;
    long start, end;
    if (!PN_IS_TUPLE(rec)) break;
    start = PN_INT(PN_TUPLE_AT(rec, 0)); end = PN_INT(PN_TUPLE_AT(rec, 1));
    r = p5_tostr(P, PN_CLOSURE_CALL2(P, closure, P->lobby, PN_NIL));
    P5_APPEND(src + last, start - last);
    P5_APPEND(PN_STR_PTR(r), PN_STR_LEN(r));
    last = end; count++;
    if (!PN_TEST(global)) break;
    if (end == start) { /* an empty match: go on one character further */
      pos = end + 1;
      while (pos < len && (src[pos] & 0xC0) == 0x80) pos++;
    } else pos = end;
  }
  P5_APPEND(src + last, len - last);
#undef P5_APPEND
  potion_regex_nsubst = count;
  result = count ? PN_STRN(buf ? buf : "", blen) : subject;
  (void)out;
  free(src); free(buf);
  return result;
}
static PN p5_subcount(Potion *P, PN cl, PN self) { return PN_NUM(potion_regex_nsubst); }

static long p5_trcount_v;
/* the character after a backslash at S[*I]: \n \t \r \f \e \a, octal \0 .. \377, \xHH and \x{H..}; any other char stands for itself. Advances *I */
static unsigned p5_tr_esc(const char *s, long len, long *ip) {
  long i = *ip;
  unsigned char e = (unsigned char)s[i];
  unsigned c;
  if (e >= '0' && e <= '7') {
    int k = 0;
    c = 0;
    while (k < 3 && i < len && s[i] >= '0' && s[i] <= '7') { c = c * 8 + (unsigned)(s[i] - '0'); i++; k++; }
  } else if (e == 'x') {
    c = 0; i++;
    if (i < len && s[i] == '{') {
      i++;
      while (i < len && s[i] != '}') { c = c * 16 + (unsigned)(isdigit((unsigned char)s[i]) ? s[i] - '0' : (tolower((unsigned char)s[i]) - 'a' + 10)); i++; }
      if (i < len) i++;
    } else {
      int k = 0;
      while (k < 2 && i < len && isxdigit((unsigned char)s[i])) { c = c * 16 + (unsigned)(isdigit((unsigned char)s[i]) ? s[i] - '0' : (tolower((unsigned char)s[i]) - 'a' + 10)); i++; k++; }
    }
  } else {
    c = e == 'n' ? '\n' : e == 't' ? '\t' : e == 'r' ? '\r' : e == 'f' ? '\f' : e == 'e' ? 27 : e == 'a' ? 7 : e;
    i++;
  }
  *ip = i;
  return c;
}
/* the code points of the tr list S: ranges a-z, escapes \n \t \r \\ \- and \x for x */
static long p5_tr_expand(const char *s, long len, unsigned *out, long max) {
  long i = 0, n = 0;
  unsigned prev = 0;
  int have_prev = 0;
  while (i < len && n < max) {
    unsigned c;
    int l = 1;
    unsigned char b = (unsigned char)s[i];
    if (b == '\\' && i + 1 < len) {
      long q = i + 1;
      c = p5_tr_esc(s, len, &q);
      i = q; l = 0;
    } else if (b >= 0xF0 && i + 3 < len) { c = ((b & 7) << 18) | ((s[i+1] & 63) << 12) | ((s[i+2] & 63) << 6) | (s[i+3] & 63); i += 4; l = 0; }
    else if (b >= 0xE0 && i + 2 < len) { c = ((b & 15) << 12) | ((s[i+1] & 63) << 6) | (s[i+2] & 63); i += 3; l = 0; }
    else if (b >= 0xC0 && i + 1 < len) { c = ((b & 31) << 6) | (s[i+1] & 63); i += 2; l = 0; }
    else { c = b; i++; }
    (void)l;
    if (c == '-' && b == '-' && have_prev && i < len) { /* a range prev-NEXT */
      unsigned hi, k;
      unsigned char e = (unsigned char)s[i];
      if (e == '\\' && i + 1 < len) { long q = i + 1; hi = p5_tr_esc(s, len, &q); i = q; }
      else if (e >= 0xF0 && i + 3 < len) { hi = ((e & 7) << 18) | ((s[i+1] & 63) << 12) | ((s[i+2] & 63) << 6) | (s[i+3] & 63); i += 4; }
      else if (e >= 0xE0 && i + 2 < len) { hi = ((e & 15) << 12) | ((s[i+1] & 63) << 6) | (s[i+2] & 63); i += 3; }
      else if (e >= 0xC0 && i + 1 < len) { hi = ((e & 31) << 6) | (s[i+1] & 63); i += 2; }
      else { hi = e; i++; }
      for (k = prev + 1; k <= hi && n < max; k++) out[n++] = k;
      have_prev = 0;
      continue;
    }
    out[n++] = c;
    prev = c; have_prev = 1;
  }
  return n;
}
static long p5_utf8_put(unsigned c, char *o) {
  if (c < 0x80) { o[0] = (char)c; return 1; }
  if (c < 0x800) { o[0] = (char)(0xC0 | (c >> 6)); o[1] = (char)(0x80 | (c & 63)); return 2; }
  if (c < 0x10000) { o[0] = (char)(0xE0 | (c >> 12)); o[1] = (char)(0x80 | ((c >> 6) & 63)); o[2] = (char)(0x80 | (c & 63)); return 3; }
  o[0] = (char)(0xF0 | (c >> 18)); o[1] = (char)(0x80 | ((c >> 12) & 63)); o[2] = (char)(0x80 | ((c >> 6) & 63)); o[3] = (char)(0x80 | (c & 63));
  return 4;
}
/* STR.p5tr(FROM, TO, FLAGS): flags c (complement FROM), d (delete), s (squeeze), r is the caller's */
static PN p5_tr_m(Potion *P, PN cl, PN self, PN from, PN to, PN flags) {
  PN str = p5_tostr(P, self);
  long len = PN_STR_LEN(str), flen = PN_STR_LEN(from), tlen = PN_STR_LEN(to), nf, nt, i, o = 0;
  unsigned *F = malloc(sizeof(unsigned) * 70000), *T = malloc(sizeof(unsigned) * 70000); /* a range a-z expands */
  char *src = malloc(len + 1), *out = malloc(len * 4 + 1);
  int c = 0, d = 0, sq = 0, lastmapped = 0;
  unsigned lastc = 0;
  const char *fl = PN_STR_PTR(flags);
  long k, fln = PN_STR_LEN(flags);
  PN result;
  if (!F || !T || !src || !out) potion_allocation_error();
  memcpy(src, PN_STR_PTR(str), len);
  nf = p5_tr_expand(PN_STR_PTR(from), flen, F, 70000);
  nt = p5_tr_expand(PN_STR_PTR(to), tlen, T, 70000);
  for (k = 0; k < fln; k++) { if (fl[k] == 'c') c = 1; else if (fl[k] == 'd') d = 1; else if (fl[k] == 's') sq = 1; }
  p5_trcount_v = 0;
  for (i = 0; i < len;) {
    unsigned ch;
    unsigned char b = (unsigned char)src[i];
    int w = 1;
    long idx = -1, j;
    if (b >= 0xF0 && i + 3 < len) { ch = ((b & 7) << 18) | ((src[i+1] & 63) << 12) | ((src[i+2] & 63) << 6) | (src[i+3] & 63); w = 4; }
    else if (b >= 0xE0 && i + 2 < len) { ch = ((b & 15) << 12) | ((src[i+1] & 63) << 6) | (src[i+2] & 63); w = 3; }
    else if (b >= 0xC0 && i + 1 < len) { ch = ((b & 31) << 6) | (src[i+1] & 63); w = 2; }
    else ch = b;
    for (j = 0; j < nf; j++) if (F[j] == ch) { idx = j; break; }
    if ((idx >= 0) != (c != 0)) { /* translated */
      unsigned m;
      int del = 0;
      p5_trcount_v++;
      if (nt == 0) { if (d) del = 1; else m = ch; }
      else if (c || idx >= nt) { if (d) del = 1; else m = T[nt - 1]; }
      else m = T[idx];
      if (del) { i += w; continue; }
      if (nt == 0) m = ch;
      if (sq && lastmapped && lastc == m) { i += w; continue; }
      o += p5_utf8_put(m, out + o);
      lastmapped = 1; lastc = m;
    } else {
      memcpy(out + o, src + i, w); o += w;
      lastmapped = 0;
    }
    i += w;
  }
  result = PN_STRN(out, o);
  free(F); free(T); free(src); free(out);
  return result;
}
static PN p5_trcount(Potion *P, PN cl, PN self) { return PN_NUM(p5_trcount_v); }

/* quotemeta: a backslash before every byte that is no word character (high bytes >= 128 are kept,
 * a byte-oriented stand-in for perl's Unicode word-char test) */
static PN p5_quotemeta_str(Potion *P, PN str) {
  str = p5_tostr(P, str);
  PN_SIZE n = PN_STR_LEN(str), i, o = 0;
  char *buf = (char *)malloc(n * 2 + 1);
  PN r;
  if (!buf) potion_allocation_error();
  for (i = 0; i < n; i++) {
    unsigned char c = (unsigned char)PN_STR_PTR(str)[i];
    if (!(isalnum(c) || c == '_' || c >= 128)) buf[o++] = '\\';
    buf[o++] = (char)c;
  }
  r = PN_STRN(buf, o);
  free(buf);
  return r;
}
static PN p5_quotemeta(Potion *P, PN cl, PN self) { return p5_quotemeta_str(P, self); }
static PN p5quotemeta_fn(Potion *P, PN cl, PN self, PN str) { return p5_quotemeta_str(P, str); }

/* 3 x 4, $n x 2: a number repeats its string form */
static PN p5_num_repeat(Potion *P, PN cl, PN self, PN count) {
  PN str = p5_tostr(P, self);
  return PN_IS_STR(str) ? potion_send(str, PN_STR("repeat"), count) : PN_NIL;
}
void p5_string_init(Potion *P) {
  { int vi; PN nv[4] = { PN_VTABLE(PN_TNUMBER), PN_VTABLE(PN_TINTEGER), PN_VTABLE(PN_TDOUBLE), PN_VTABLE(PN_TBIGINT) };
    for (vi = 0; vi < 4; vi++) potion_method(nv[vi], "repeat", p5_num_repeat, "count=o"); }
  potion_define_global(P, PN_STR("$\""), PN_STR(" ")); /* the list separator of interpolated arrays */
  potion_define_global(P, PN_STR("$]"), PN_STR("5.042003")); /* the perl version we emulate */
  potion_define_global(P, PN_STR("$["), PN_ZERO); /* the array base, deprecated, always 0 */
  potion_define_global(P, PN_STR("$^W"), PN_ZERO);
  potion_define_global(P, PN_STR("$^CHOPPED"), PN_STR0);
  potion_define_global(P, PN_STR("$^CHOMPED"), PN_ZERO);
  potion_define_global(P, PN_STR("$^GLOBAL_PHASE"), PN_STR("START")); /* BEGIN blocks; the program sets CHECK, INIT, RUN, END */
  potion_define_global(P, PN_STR("$^T"), PN_NUM((long)time(NULL)));
#if defined(_WIN32)
  potion_define_global(P, PN_STR("$^O"), PN_STR("MSWin32"));
#elif defined(__APPLE__)
  potion_define_global(P, PN_STR("$^O"), PN_STR("darwin"));
#elif defined(__FreeBSD__)
  potion_define_global(P, PN_STR("$^O"), PN_STR("freebsd"));
#elif defined(__NetBSD__)
  potion_define_global(P, PN_STR("$^O"), PN_STR("netbsd"));
#elif defined(__OpenBSD__)
  potion_define_global(P, PN_STR("$^O"), PN_STR("openbsd"));
#else
  potion_define_global(P, PN_STR("$^O"), PN_STR("linux"));
#endif
  {
    PN rv[5] = { PN_VTABLE(PN_TNUMBER), PN_VTABLE(PN_TINTEGER), PN_VTABLE(PN_TDOUBLE),
                 PN_VTABLE(PN_TNIL), PN_VTABLE(PN_TOBJECT) };
    int i;
    for (i = 0; i < 5; i++) potion_method(rv[i], "match", p5_any_match, "pattern=S");
    for (i = 0; i < 5; i++) potion_method(rv[i], "quotemeta", p5_quotemeta, 0);
    potion_method(PN_VTABLE(PN_TSTRING), "quotemeta", p5_quotemeta, 0);
  }
  potion_method(P->lobby, "split", p5_split, "pat=o,str=o,limit=o");
  potion_method(P->lobby, "p5lprint", p5_lprint, 0);
  potion_method(P->lobby, "p5lsay", p5_lsay, 0);
  PN str_vt = PN_VTABLE(PN_TSTRING);
  potion_method(P->lobby, "p5match", p5_match_m, "pattern=o");
  potion_method(P->lobby, "p5reset", p5_reset, "arg=o");
  potion_method(P->lobby, "p5matchl", p5_matchl, "pattern=o");
  potion_method(P->lobby, "p5matchg", p5_matchg, "pattern=o,key=o,keep=o");
  potion_method(P->lobby, "p5pos", p5_pos, "key=o,subject=o");
  potion_method(P->lobby, "p5posset", p5_posset, "key=o,subject=o,val=o");
  potion_method(P->lobby, "p5matchall", p5_matchall, "pattern=o");
  potion_method(P->lobby, "p5subst", p5_subst_m, "pattern=o,repl=o,global=o");
  potion_method(P->lobby, "p5substf", p5_substf, "pattern=o,closure=o,global=o");
  potion_method(P->lobby, "p5subcount", p5_subcount, 0);
  potion_method(P->lobby, "p5tr", p5_tr_m, "from=o,to=o,flags=o");
  potion_method(P->lobby, "p5trcount", p5_trcount, 0);
  potion_method(str_vt, "p5chomp", p5_chomp, 0);
  potion_method(str_vt, "p5chop", p5_chop, 0);
  potion_method(str_vt, "p5substrset", p5_substrset, "off=o,len=o,repl=o");
  potion_method(PN_VTABLE(PN_TNIL), "p5chomp", p5_chomp_id, 0);
  potion_method(PN_VTABLE(PN_TTUPLE), "p5chomp", p5_chomp_list, 0);
  potion_method(PN_VTABLE(PN_TTUPLE), "p5chop", p5_chop_list, 0);
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
  potion_method(P->lobby, "quotemeta", p5quotemeta_fn, "str=o");
  potion_method(PN_VTABLE(PN_TTUPLE), "p5joins", p5_joins, "sep=o");
  potion_method(PN_VTABLE(PN_TTABLE), "p5joins", p5_joins, "sep=o");
  potion_method(PN_VTABLE(PN_TSTRING), "p5joins", p5_joins, "sep=o");
  potion_method(P->lobby, "sprintf", p5_sprintf, "fmt=S|a=o,b=o,c=o,d=o,e=o,f=o");
}
