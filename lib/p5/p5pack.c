/**\file lib/p5/p5pack.c
  p5 pack/unpack: template-driven binary <-> list conversion.
  pack(TPL, LIST)  -> LIST.p5pack(TPL)   (the flattened list is the receiver)
  unpack(TPL, STR) -> STR.p5unpack(TPL)

  String types (a A Z z h H B b) consume ONE value per directive; the count is
  the number of characters (h/hex: chars, B/b: bits). Numeric types (C c W o
  s S i I l L n N v V f F d D e E w U) consume COUNT values, each packed into
  a fixed-width unit. x X @ . are position directives and consume no value.

  (c) 2026 perl11.com */
#include "p5.h"
#include <ctype.h>

/* ---- growable output buffer (pack) ---- */
struct p5pk {
  char *b;
  size_t n, cap;
};
static void p5pk_ensure(struct p5pk *o, size_t extra) {
  if (o->n + extra > o->cap) {
    size_t nc = o->cap ? o->cap : 64;
    while (o->n + extra > nc) nc *= 2;
    o->b = (char *)realloc(o->b, nc);
    o->cap = nc;
  }
}
static void p5pk_byte(struct p5pk *o, int c) {
  p5pk_ensure(o, 1);
  o->b[o->n++] = (char)c;
}
static void p5pk_raw(struct p5pk *o, const void *p, size_t n) {
  p5pk_ensure(o, n);
  memcpy(o->b + o->n, p, n);
  o->n += n;
}
static void p5pk_padto(struct p5pk *o, size_t to, char pad) { /* fill forward */
  if (to > o->n) {
    p5pk_ensure(o, to - o->n);
    memset(o->b + o->n, pad, to - o->n);
    o->n = to;
  }
}

/* ---- value accessors ---- */
static const char *p5pk_cstr(Potion *P, PN v, size_t *len) {
  static const char e[1] = { 0 };
  PN s = p5_tostr(P, v);
  if (PN_IS_STR(s)) { *len = PN_STR_LEN(s); return PN_STR_PTR(s); }
  *len = 0; return e;
}
static double p5pk_dbl(PN v) {
  if (PN_IS_INT(v)) return (double)PN_INT(v);
  if (PN_IS_DBL(v)) return PN_DBL(v);
  if (PN_IS_NUM(v)) return (double)PN_INT(v);
  if (PN_IS_STR(v)) return strtod(PN_STR_PTR(v), NULL);
  return 0.0;
}
static long long p5pk_ll(PN v) {
  if (PN_IS_INT(v)) return PN_INT(v);
  if (PN_IS_DBL(v)) return (long long)PN_DBL(v);
  if (PN_IS_NUM(v)) return PN_INT(v);
  if (PN_IS_STR(v)) return strtoll(PN_STR_PTR(v), NULL, 10);
  return 0;
}

/* fixed-width integer store/load */
static void p5pk_u(struct p5pk *o, unsigned long long v, int n, int be) {
  char t[8];
  for (int i = 0; i < n; i++) t[i] = (char)(v >> (8 * (be ? n - 1 - i : i)));
  p5pk_raw(o, t, n);
}
static unsigned long long p5pk_get(const unsigned char *p, int n, int be) {
  unsigned long long v = 0;
  for (int i = 0; i < n; i++) v = (v << 8) | p[be ? i : n - 1 - i];
  return v;
}
static void p5pk_nat_i(struct p5pk *o, long long v, int n) { /* little-endian */
  char t[8];
  for (int i = 0; i < n; i++) t[i] = (char)((unsigned long long)v >> (8 * i));
  p5pk_raw(o, t, n);
}
static long long p5pk_get_nat(const unsigned char *p, int n) {
  unsigned long long v = 0;
  for (int i = n - 1; i >= 0; i--) v = (v << 8) | p[i];
  if (n < 8 && (v >> (8 * n - 1))) v |= ~0ULL << (8 * n);
  return (long long)v;
}

/* ---- a parsed template directive ---- */
struct p5dir {
  char type;
  int  count;   /* 0 = one (default), >0 = explicit, -1 = * (rest) */
  int  star, slash, bang;
};
static int p5dir_parse(const char *t, size_t tlen, size_t i, struct p5dir *d) {
  d->type = t[i]; d->count = 0; d->star = 0; d->slash = 0; d->bang = 0;
  size_t j = i + 1;
  if (j < tlen && t[j] == '!') { d->bang = 1; j++; }
  if (j < tlen && t[j] == '*') { d->count = -1; d->star = 1; j++; }
  else if (j < tlen && t[j] == '/') { d->slash = 1; j++; }
  else if (j < tlen && t[j] >= '0' && t[j] <= '9') {
    long c = 0;
    while (j < tlen && t[j] >= '0' && t[j] <= '9') c = c * 10 + (t[j++] - '0');
    d->count = (int)c;
  }
  return (int)(j - i);
}

/* one value, N characters: the string (hex / bit) types */
static int p5pk_onestr(char c) {
  return c=='a'||c=='A'||c=='Z'||c=='z'||c=='h'||c=='H'||c=='B'||c=='b';
}

/* pack one numeric value into fixed bytes (width per type, bang-aware) */
static void p5pk_packnum(struct p5pk *o, char c, int bang, PN v) {
  long long x = p5pk_ll(v);
  double dbl = p5pk_dbl(v);
  switch (c) {
    case 'C': case 'W': case 'o': case 'c': p5pk_byte(o, (int)(x & 0xFF)); break;
    case 's': case 'S': p5pk_nat_i(o, x, 2); break;
    case 'i': case 'I': p5pk_nat_i(o, x, 4); break;
    case 'l': case 'L': p5pk_nat_i(o, x, (bang ? 8 : 4)); break;
    case 'n': p5pk_u(o, (unsigned long long)x, 2, 1); break;
    case 'v': p5pk_u(o, (unsigned long long)x, 2, 0); break;
    case 'N': p5pk_u(o, (unsigned long long)x, 4, 1); break;
    case 'V': p5pk_u(o, (unsigned long long)x, 4, 0); break;
    case 'f': { float f = (float)dbl; p5pk_raw(o, &f, 4); break; }
    case 'F': case 'd': case 'D': { unsigned long long bits; memcpy(&bits, &dbl, 8); p5pk_u(o, bits, 8, 0); break; }
    case 'e': { unsigned long long bits; memcpy(&bits, &dbl, 8); p5pk_u(o, bits, 8, 0); break; }
    case 'E': { unsigned long long bits; memcpy(&bits, &dbl, 8); p5pk_u(o, bits, 8, 1); break; }
    case 'w': { unsigned long long u = (unsigned long long)x;
               if (u == 0) { p5pk_byte(o, 0); break; }
               unsigned char tmp[16]; int nt = 0;
               while (u) { tmp[nt++] = (unsigned char)(u & 0x7F); u >>= 7; }
               for (int b = 1; b < nt; b++) tmp[b] |= 0x80; /* all but the last (LSB) byte carry the continue bit */
               for (int b = nt - 1; b >= 0; b--) p5pk_byte(o, tmp[b]);
               break; }
    case 'U': case 'U0': p5pk_byte(o, (int)(x & 0xFF)); break;
    default: break;
  }
}
static PN p5pack(Potion *P, PN cl, PN self, PN tpl) {
  const char *t = PN_STR_PTR(tpl);
  size_t tlen = PN_STR_LEN(tpl);
  struct p5pk o; o.b = NULL; o.n = 0; o.cap = 0;
  long vi = 0, nvals = PN_IS_TUPLE(self) ? (long)PN_TUPLE_LEN(self) : 0;

  for (size_t i = 0; i < tlen; ) {
    char c = t[i];
    if (c == ' ' || c == '\t') { i++; continue; } /* perl ignores whitespace in templates */
    if (c == '(' || c == ')') { i++; continue; }
    if (c == '.') { o.n = 0; i++; continue; }
    if (c == 'x' || c == 'X') {
      long n = 0; size_t j = i + 1;
      while (j < tlen && t[j] >= '0' && t[j] <= '9') n = n * 10 + (t[j++] - '0');
      if (n == 0) n = 1;
      if (c == 'x') p5pk_padto(&o, o.n + n, 0);
      else if (o.n >= (size_t)n) o.n -= n;
      i = j; continue;
    }
    if (c == '@') { size_t j = i + 1;
      while (j < tlen && t[j] >= '0' && t[j] <= '9') j++;
      i = j; continue; } /* perl 5.42: @ is a no-op */
    if (c == '%') { long n = 0; size_t j = i + 1;
      while (j < tlen && t[j] >= '0' && t[j] <= '9') n = n * 10 + (t[j++] - '0');
      if (n == 0) n = 1;
      long long s = 0;
      for (long long k = (long long)o.n - n; k < (long long)o.n; k++) if (k >= 0) s += (unsigned char)o.b[k];
      p5pk_byte(&o, (int)(s & 0xFF)); i = j; continue; }

    struct p5dir d;
    int adv = p5dir_parse(t, tlen, i, &d);
    if (adv <= 0) { i++; continue; }
    i += adv;

    if (p5pk_onestr(d.type)) {
      PN v = (vi < nvals) ? PN_TUPLE_AT(self, vi++) : PN_NIL;
      const char *s; size_t slen;
      s = p5pk_cstr(P, v, &slen);
      long want = d.star ? (long)slen : (d.count > 0 ? d.count : 1);
      switch (d.type) {
        case 'a': { long m = (long)slen < want ? (long)slen : want;
                    p5pk_raw(&o, s, m); break; }
        case 'A': { long m = (long)slen < want ? (long)slen : want;
                    p5pk_raw(&o, s, m); p5pk_padto(&o, (size_t)want, ' '); break; }
        case 'Z': { long m = (long)slen + 1 < want ? (long)slen : want - 1;
                    if (m < 0) m = 0; p5pk_raw(&o, s, m); p5pk_byte(&o, 0); break; }
        case 'z': { long m = (long)slen;
                    while (m > 0 && (s[m-1]==' '||s[m-1]=='\t')) m--;
                    if (want > 0 && m > want) m = want;
                    p5pk_raw(&o, s, m); p5pk_byte(&o, ' '); break; }
        case 'h': case 'H': {
                    const char *hx = "0123456789abcdef";
                    for (long k = 0; k < want; k += 2) {
                      int a = 0, b = 0;
                      if (k < slen) { char ch = s[k];
                        if (ch>='0'&&ch<='9') a=ch-'0';
                        else if (ch>='a'&&ch<='f') a=ch-'a'+10;
                        else if (ch>='A'&&ch<='F') a=ch-'A'+10; }
                      if (k+1 < slen) { char ch = s[k+1];
                        if (ch>='0'&&ch<='9') b=ch-'0';
                        else if (ch>='a'&&ch<='f') b=ch-'a'+10;
                        else if (ch>='A'&&ch<='F') b=ch-'A'+10; }
                      if (d.type == 'H') p5pk_byte(&o, (a<<4)|b); /* H: first char high */
                      else p5pk_byte(&o, (b<<4)|a);              /* h: first char low */
                      (void)hx;
                    } break; }
        case 'B': case 'b': {
                    for (long k = 0; k < want; k += 8) {
                      unsigned char by = 0;
                      for (int p = 0; p < 8 && k + p < slen; p++) {
                        int bv = s[k + p] == '1';
                        if (d.type == 'B') by |= (unsigned char)(bv << (7 - p)); /* MSB first */
                        else by |= (unsigned char)(bv << p);                    /* LSB first */
                      }
                      p5pk_byte(&o, by);
                    } break; }
      }
    } else {
      long cnt;
      if (d.slash) { cnt = (vi < nvals) ? p5pk_ll(PN_TUPLE_AT(self, vi++)) : 0; if (cnt < 0) cnt = 0; }
      else if (d.star) cnt = nvals - vi;
      else cnt = d.count > 0 ? d.count : 1;
      for (long k = 0; k < cnt; k++) {
        PN v = (vi < nvals) ? PN_TUPLE_AT(self, vi++) : PN_NIL;
        p5pk_packnum(&o, d.type, d.bang, v);
      }
    }
  }
  PN r = o.n ? potion_byte_str2(P, o.b, o.n) : potion_byte_str2(P, "", 0);
  free(o.b);
  return r;
}

/* ---- unpack ---- */
static PN p5unpack_parse(Potion *P, PN self, PN tpl) {
  const char *t = PN_STR_PTR(tpl);
  size_t tlen = PN_STR_LEN(tpl);
  const unsigned char *b = (const unsigned char *)PN_STR_PTR(self);
  size_t blen = PN_STR_LEN(self);
  size_t pos = 0;
  PN out = PN_TUP0();
  #define PUSHVAL(x) (out = PN_PUSH(out, (x)))

  for (size_t i = 0; i < tlen; ) {
    char c = t[i];
    if (c == ' ' || c == '\t') { i++; continue; } /* perl ignores whitespace in templates */
    if (c == '(' || c == ')' || c == '!') { i++; continue; }
    if (c == '.') { pos = 0; i++; continue; }
    if (c == '@') { size_t j = i + 1;
      while (j < tlen && t[j] >= '0' && t[j] <= '9') j++;
      i = j; continue; } /* perl 5.42: @ is a no-op */
    if (c == 'x') { long n = 0; size_t j = i + 1;
      while (j < tlen && t[j] >= '0' && t[j] <= '9') n = n * 10 + (t[j++] - '0');
      if (n == 0) n = 1;
      pos = (pos + n <= blen) ? pos + n : blen; i = j; continue; }
    if (c == 'X') { long n = 0; size_t j = i + 1;
      while (j < tlen && t[j] >= '0' && t[j] <= '9') n = n * 10 + (t[j++] - '0');
      if (n == 0) n = 1;
      pos = (pos > (size_t)n) ? pos - (size_t)n : 0; i = j; continue; }

    struct p5dir d;
    int adv = p5dir_parse(t, tlen, i, &d);
    if (adv <= 0) { i++; continue; }
    i += adv;

    if (p5pk_onestr(d.type)) {
      long want = d.star ? (long)(blen - pos) : (d.count > 0 ? d.count : 1);
      if (want < 0) want = 0;
      if (d.type == 'a' || d.type == 'A') {
        long m = want;
        if (m > (long)(blen - pos)) m = (long)(blen - pos);
        if (m < 0) m = 0;
        if (d.type == 'A') while (m > 0 && b[pos + m - 1] == ' ') m--;
        PUSHVAL(potion_byte_str2(P, (const char *)b + pos, m));
        pos += (size_t)m;
      } else if (d.type == 'h' || d.type == 'H') {
        long bytes, outlen;
        if (d.star) { bytes = (long)(blen - pos); outlen = bytes * 2; }
        else { outlen = (d.count > 0 ? d.count : 2); bytes = (outlen + 1) / 2; }
        if (bytes > (long)(blen - pos)) { bytes = (long)(blen - pos); outlen = bytes * 2; }
        if (bytes < 0) bytes = 0;
        const char *hx = "0123456789abcdef";
        char *buf = (char *)malloc(outlen + 1);
        for (long k = 0; k < bytes; k++) {
          int hv = b[pos + k];
          if (d.type == 'h') { buf[2*k] = hx[hv & 0xF]; buf[2*k+1] = hx[(hv >> 4) & 0xF]; } /* low nibble first */
          else { buf[2*k] = hx[(hv >> 4) & 0xF]; buf[2*k+1] = hx[hv & 0xF]; } /* H: high first, lowercase (perl) */
        }
        buf[outlen] = 0;
        PUSHVAL(potion_str2(P, buf, outlen)); free(buf);
        pos += (size_t)bytes;
      } else { /* B b: want is the number of bits; star = all remaining bits */
        long bits = d.star ? (long)(blen - pos) * 8 : want;
        long bytes = (bits + 7) / 8;
        if (bytes > (long)(blen - pos)) { bytes = (long)(blen - pos); bits = bytes * 8; }
        if (bytes < 0) bytes = 0;
        char *buf = (char *)malloc(bits + 1);
        for (long bit = 0; bit < bits; bit++) {
          int hv = b[pos + bit / 8];
          int bi = (int)(bit % 8);
          int bv = (hv >> (d.type == 'B' ? 7 - bi : bi)) & 1;
          buf[bit] = bv ? '1' : '0';
        }
        buf[bits] = 0;
        PUSHVAL(potion_str2(P, buf, bits)); free(buf);
        pos += (size_t)bytes;
      }
    } else {
      long cnt = d.star ? (long)(blen - pos) : (d.count > 0 ? d.count : 1);
      if (cnt < 0) cnt = 0;
      for (long k = 0; k < cnt; k++) {
        switch (d.type) {
          case 'C': case 'W': case 'o': case 'U': case 'U0': PUSHVAL(pos < blen ? PN_NUM((int)b[pos]) : PN_NUM(0)); pos++; break;
          case 'c': PUSHVAL(pos < blen ? PN_NUM((int)(signed char)b[pos]) : PN_NUM(0)); pos++; break;
          case 's': PUSHVAL(pos+2<=blen ? PN_NUM((int)p5pk_get_nat(b+pos,2)) : PN_NUM(0)); pos += 2; break;
          case 'S': PUSHVAL(pos+2<=blen ? PN_NUM((long)p5pk_get(b+pos,2,0)) : PN_NUM(0)); pos += 2; break;
          case 'i': PUSHVAL(pos+4<=blen ? PN_NUM((long)p5pk_get_nat(b+pos,4)) : PN_NUM(0)); pos += 4; break;
          case 'I': PUSHVAL(pos+4<=blen ? PN_NUM((long)p5pk_get(b+pos,4,0)) : PN_NUM(0)); pos += 4; break;
          case 'l': { int w = d.bang ? 8 : 4;
              PUSHVAL(pos+(size_t)w<=blen ? PN_NUM(p5pk_get_nat(b+pos,w)) : PN_NUM(0)); pos += w; break; }
          case 'L': { int w = d.bang ? 8 : 4;
              PUSHVAL(pos+(size_t)w<=blen ? PN_NUM((long)p5pk_get(b+pos,w,0)) : PN_NUM(0)); pos += w; break; }
          case 'n': PUSHVAL(pos+2<=blen ? PN_NUM((int)p5pk_get(b+pos,2,1)) : PN_NUM(0)); pos += 2; break;
          case 'v': PUSHVAL(pos+2<=blen ? PN_NUM((int)p5pk_get(b+pos,2,0)) : PN_NUM(0)); pos += 2; break;
          case 'N': PUSHVAL(pos+4<=blen ? PN_NUM((long)p5pk_get(b+pos,4,1)) : PN_NUM(0)); pos += 4; break;
          case 'V': PUSHVAL(pos+4<=blen ? PN_NUM((long)p5pk_get(b+pos,4,0)) : PN_NUM(0)); pos += 4; break;
          case 'f': { if (pos+4<=blen) { float f; memcpy(&f,b+pos,4); PUSHVAL(potion_double(P, f)); } else PUSHVAL(PN_NUM(0)); pos += 4; break; }
          case 'd': case 'D': case 'F': { if (pos+8<=blen) { double d; memcpy(&d,b+pos,8); PUSHVAL(potion_double(P, d)); } else PUSHVAL(PN_NUM(0)); pos += 8; break; }
          case 'h': case 'H': {
            if (pos < blen) {
              const char *hx = "0123456789abcdef"; int hv = b[pos];
              char hs[3] = { (char)hx[(hv>>4)&0xF], (char)hx[hv&0xF], 0 };
              if (d.type == 'H') { hs[0]=toupper(hs[0]); hs[1]=toupper(hs[1]); }
              PUSHVAL(potion_str(P, hs));
            } else PUSHVAL(PN_STR0);
            pos++; break; }
          case 'w': { /* BER bignum */
            unsigned long long u = 0;
            while (pos < blen) { int by = b[pos++]; u = (u << 7) | (by & 0x7F); if (!(by & 0x80)) break; }
            PUSHVAL(PN_NUM((long)u)); break; }
          default: if (pos < blen) PUSHVAL(PN_NUM((int)b[pos])); pos++; break;
        }
      }
    }
  }
  #undef PUSHVAL
  return out;
}
/* unpack in scalar context: the first value (perl semantics); list context: the whole list */
static PN p5unpack(Potion *P, PN cl, PN self, PN tpl) {
  PN out = p5unpack_parse(P, self, tpl);
  return PN_IS_TUPLE(out) && PN_TUPLE_LEN(out) > 0 ? PN_TUPLE_AT(out, 0) : PN_NIL;
}
static PN p5unpackl(Potion *P, PN cl, PN self, PN tpl) {
  return p5unpack_parse(P, self, tpl);
}

void p5_pack_init(Potion *P) {
  /* pack: the receiver is the (flattened) list of values -> tuple (or a lone scalar) */
  potion_method(PN_VTABLE(PN_TTUPLE), "p5pack", p5pack, "tpl=o");
  potion_method(PN_VTABLE(PN_TSTRING), "p5pack", p5pack, "tpl=o");
  potion_method(PN_VTABLE(PN_TBYTES), "p5pack", p5pack, "tpl=o");
  /* unpack: the receiver is the input byte string; p5unpack = scalar (first value),
   * p5unpackl = list (p5_listctx renames it in list contexts) */
  potion_method(PN_VTABLE(PN_TSTRING), "p5unpack", p5unpack, "tpl=o");
  potion_method(PN_VTABLE(PN_TBYTES), "p5unpack", p5unpack, "tpl=o");
  potion_method(PN_VTABLE(PN_TTUPLE), "p5unpack", p5unpack, "tpl=o");
  potion_method(PN_VTABLE(PN_TSTRING), "p5unpackl", p5unpackl, "tpl=o");
  potion_method(PN_VTABLE(PN_TBYTES), "p5unpackl", p5unpackl, "tpl=o");
  potion_method(PN_VTABLE(PN_TTUPLE), "p5unpackl", p5unpackl, "tpl=o");
}
