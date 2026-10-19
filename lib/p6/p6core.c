/* p6core.c - Raku core methods and functions on numbers, strings and lists (included by libp6.c) */

static PN p6c_num(Potion *P, double d) {
  if (d == floor(d) && fabs(d) < 9007199254740992.0) return PN_NUM((long)d);
  return potion_double(P, d);
}
static double p6c_dbl(PN v) { return PN_IS_NUM(v) ? PN_DBL(v) : (PN_IS_STR(v) ? atof(PN_STR_PTR(v)) : 0); }
static PN p6c_str(Potion *P, PN v) {
  char b[64];
  if (PN_IS_STR(v)) return v;
  if (PN_IS_TUPLE(v)) { PN s = potion_send(v, PN_string); if (PN_IS_STR(s)) return s; }
  return potion_str(P, p6_coerce_str(P, v, b, sizeof b));
}

/* ---- numbers ---- */
#define P6C_NUM1(name, expr) static PN name(Potion *P, PN cl, PN self) { double x = p6c_dbl(self); return p6c_num(P, (expr)); }
P6C_NUM1(p6c_abs, fabs(x))
P6C_NUM1(p6c_floor, floor(x))
P6C_NUM1(p6c_ceiling, ceil(x))
P6C_NUM1(p6c_round, floor(x + 0.5))
P6C_NUM1(p6c_truncate, trunc(x))
P6C_NUM1(p6c_succ, x + 1)
P6C_NUM1(p6c_pred, x - 1)
P6C_NUM1(p6c_sqrt_, sqrt(x))
P6C_NUM1(p6c_exp_, exp(x))
P6C_NUM1(p6c_sin_, sin(x))
P6C_NUM1(p6c_cos_, cos(x))
P6C_NUM1(p6c_sign, x > 0 ? 1 : x < 0 ? -1 : 0)
P6C_NUM1(p6c_tan_, tan(x))
static PN p6c_f_tan(Potion *P, PN cl, PN self, PN a) { return p6c_tan_(P, cl, a); }
P6C_NUM1(p6c_asin_, asin(x))
static PN p6c_f_asin(Potion *P, PN cl, PN self, PN a) { return p6c_asin_(P, cl, a); }
P6C_NUM1(p6c_acos_, acos(x))
static PN p6c_f_acos(Potion *P, PN cl, PN self, PN a) { return p6c_acos_(P, cl, a); }
P6C_NUM1(p6c_atan_, atan(x))
static PN p6c_f_atan(Potion *P, PN cl, PN self, PN a) { return p6c_atan_(P, cl, a); }
P6C_NUM1(p6c_sinh_, sinh(x))
static PN p6c_f_sinh(Potion *P, PN cl, PN self, PN a) { return p6c_sinh_(P, cl, a); }
P6C_NUM1(p6c_cosh_, cosh(x))
static PN p6c_f_cosh(Potion *P, PN cl, PN self, PN a) { return p6c_cosh_(P, cl, a); }
P6C_NUM1(p6c_tanh_, tanh(x))
static PN p6c_f_tanh(Potion *P, PN cl, PN self, PN a) { return p6c_tanh_(P, cl, a); }
P6C_NUM1(p6c_asinh_, asinh(x))
static PN p6c_f_asinh(Potion *P, PN cl, PN self, PN a) { return p6c_asinh_(P, cl, a); }
P6C_NUM1(p6c_acosh_, acosh(x))
static PN p6c_f_acosh(Potion *P, PN cl, PN self, PN a) { return p6c_acosh_(P, cl, a); }
P6C_NUM1(p6c_atanh_, atanh(x))
static PN p6c_f_atanh(Potion *P, PN cl, PN self, PN a) { return p6c_atanh_(P, cl, a); }
P6C_NUM1(p6c_log10_, log10(x))
static PN p6c_f_log10(Potion *P, PN cl, PN self, PN a) { return p6c_log10_(P, cl, a); }
P6C_NUM1(p6c_log2_, log2(x))
static PN p6c_f_log2(Potion *P, PN cl, PN self, PN a) { return p6c_log2_(P, cl, a); }
P6C_NUM1(p6c_cotan_, 1/tan(x))
static PN p6c_f_cotan(Potion *P, PN cl, PN self, PN a) { return p6c_cotan_(P, cl, a); }
P6C_NUM1(p6c_sec_, 1/cos(x))
static PN p6c_f_sec(Potion *P, PN cl, PN self, PN a) { return p6c_sec_(P, cl, a); }
P6C_NUM1(p6c_cosec_, 1/sin(x))
static PN p6c_f_cosec(Potion *P, PN cl, PN self, PN a) { return p6c_cosec_(P, cl, a); }
static PN p6c_f_atan2(Potion *P, PN cl, PN self, PN a, PN b) { return p6c_num(P, atan2(p6c_dbl(a), p6c_dbl(b))); }
static PN p6c_f_truncate(Potion *P, PN cl, PN self, PN a) { return p6c_truncate(P, cl, a); }
static PN p6c_log_(Potion *P, PN cl, PN self) { double x = p6c_dbl(self); return x > 0 ? p6c_num(P, log(x)) : potion_double(P, NAN); }
static PN p6c_gcd_(Potion *P, PN cl, PN self, PN o) { long a = labs((long)p6c_dbl(self)), b = labs((long)p6c_dbl(o)); while (b) { long t = a % b; a = b; b = t; } return PN_NUM(a); }
static PN p6c_lcm_(Potion *P, PN cl, PN self, PN o) { long a = labs((long)p6c_dbl(self)), b = labs((long)p6c_dbl(o)), g = a, h = b; while (h) { long t = g % h; g = h; h = t; } return PN_NUM(g ? a / g * b : 0); }
static PN p6c_chr_(Potion *P, PN cl, PN self) {
  long c = (long)p6c_dbl(self);
  char b[8]; int n = 0;
  if (c < 0x80) b[n++] = (char)c;
  else if (c < 0x800) { b[n++] = 0xC0 | (c >> 6); b[n++] = 0x80 | (c & 0x3F); }
  else if (c < 0x10000) { b[n++] = 0xE0 | (c >> 12); b[n++] = 0x80 | ((c >> 6) & 0x3F); b[n++] = 0x80 | (c & 0x3F); }
  else { b[n++] = 0xF0 | (c >> 18); b[n++] = 0x80 | ((c >> 12) & 0x3F); b[n++] = 0x80 | ((c >> 6) & 0x3F); b[n++] = 0x80 | (c & 0x3F); }
  return potion_str2(P, b, n);
}
static PN p6c_is_prime(Potion *P, PN cl, PN self) { long n = (long)p6c_dbl(self), i; if (n < 2) return PN_FALSE; for (i = 2; i * i <= n; i++) if (n % i == 0) return PN_FALSE; return PN_TRUE; }

/* ---- strings ---- */
static long p6c_u8off(PN s, long charidx) { /* byte offset of the CHARIDX-th character */
  long i = 0, c = 0, n = (long)PN_STR_LEN(s);
  while (i < n && c < charidx) { i++; while (i < n && (((unsigned char)PN_STR_PTR(s)[i]) & 0xC0) == 0x80) i++; c++; }
  return i;
}
static long p6c_nchars(PN s) { long i, n = 0; for (i = 0; i < (long)PN_STR_LEN(s); i++) if ((((unsigned char)PN_STR_PTR(s)[i]) & 0xC0) != 0x80) n++; return n; }
static PN p6c_substr(Potion *P, PN cl, PN self, PN from, PN len) {
  long n = p6c_nchars(self), f = (long)p6c_dbl(from), l;
  if (f < 0) f += n;
  if (f < 0 || f > n) return PN_NIL;
  l = len == PN_NIL ? n - f : (long)p6c_dbl(len);
  if (l < 0) l = 0;
  if (f + l > n) l = n - f;
  { long a = p6c_u8off(self, f), b = p6c_u8off(self, f + l);
    return potion_str2(P, PN_STR_PTR(self) + a, b - a); }
}
static long p6c_find(PN h, PN needle, long startbyte) {
  const char *p = PN_STR_PTR(h), *q = PN_STR_PTR(needle);
  long hl = (long)PN_STR_LEN(h), nl = (long)PN_STR_LEN(needle), i;
  for (i = startbyte; i + nl <= hl; i++) if (!memcmp(p + i, q, nl)) return i;
  return -1;
}
static long p6c_charidx(PN s, long byteoff) { long i, c = 0; for (i = 0; i < byteoff; i++) if ((((unsigned char)PN_STR_PTR(s)[i]) & 0xC0) != 0x80) c++; return c; }
static PN p6c_index(Potion *P, PN cl, PN self, PN needle, PN pos) {
  long st = pos == PN_NIL ? 0 : p6c_u8off(self, (long)p6c_dbl(pos)), r;
  needle = p6c_str(P, needle);
  r = p6c_find(self, needle, st);
  return r < 0 ? PN_NIL : PN_NUM(p6c_charidx(self, r));
}
static PN p6c_rindex(Potion *P, PN cl, PN self, PN needle) {
  long r = -1, i = 0;
  needle = p6c_str(P, needle);
  while ((i = p6c_find(self, needle, i)) >= 0) { r = i; i++; }
  return r < 0 ? PN_NIL : PN_NUM(p6c_charidx(self, r));
}
static PN p6c_contains(Potion *P, PN cl, PN self, PN needle) { return p6c_find(self, p6c_str(P, needle), 0) >= 0 ? PN_TRUE : PN_FALSE; }
static PN p6c_starts_with(Potion *P, PN cl, PN self, PN n) { n = p6c_str(P, n); return PN_STR_LEN(n) <= PN_STR_LEN(self) && !memcmp(PN_STR_PTR(self), PN_STR_PTR(n), PN_STR_LEN(n)) ? PN_TRUE : PN_FALSE; }
static PN p6c_ends_with(Potion *P, PN cl, PN self, PN n) { n = p6c_str(P, n); return PN_STR_LEN(n) <= PN_STR_LEN(self) && !memcmp(PN_STR_PTR(self) + PN_STR_LEN(self) - PN_STR_LEN(n), PN_STR_PTR(n), PN_STR_LEN(n)) ? PN_TRUE : PN_FALSE; }
static PN p6c_map_chars(Potion *P, PN self, int (*f)(int), int first_only) {
  long i, n = (long)PN_STR_LEN(self);
  char *b = malloc(n + 1);
  PN r;
  if (!b) potion_allocation_error();
  for (i = 0; i < n; i++) { unsigned char c = (unsigned char)PN_STR_PTR(self)[i]; b[i] = (first_only && i) ? (char)c : (c < 0x80 ? (char)f(c) : (char)c); }
  r = potion_str2(P, b, n); free(b); return r;
}
static PN p6c_uc(Potion *P, PN cl, PN self) { return p6c_map_chars(P, p6c_str(P, self), toupper, 0); }
static PN p6c_lc(Potion *P, PN cl, PN self) { return p6c_map_chars(P, p6c_str(P, self), tolower, 0); }
static PN p6c_ucfirst(Potion *P, PN cl, PN self) { return p6c_map_chars(P, p6c_str(P, self), toupper, 1); }
static PN p6c_lcfirst(Potion *P, PN cl, PN self) { return p6c_map_chars(P, p6c_str(P, self), tolower, 1); }
static PN p6c_flip(Potion *P, PN cl, PN self) {
  long n = (long)PN_STR_LEN(self), i = n, o = 0;
  char *b = malloc(n + 1);
  PN r;
  if (!b) potion_allocation_error();
  while (i > 0) { long e = i; i--; while (i > 0 && (((unsigned char)PN_STR_PTR(self)[i]) & 0xC0) == 0x80) i--; memcpy(b + o, PN_STR_PTR(self) + i, e - i); o += e - i; }
  r = potion_str2(P, b, n); free(b); return r;
}
static PN p6c_trim_impl(Potion *P, PN self, int l, int r) {
  long a = 0, e = (long)PN_STR_LEN(self);
  while (l && a < e && isspace((unsigned char)PN_STR_PTR(self)[a])) a++;
  while (r && e > a && isspace((unsigned char)PN_STR_PTR(self)[e - 1])) e--;
  return potion_str2(P, PN_STR_PTR(self) + a, e - a);
}
static PN p6c_trim(Potion *P, PN cl, PN self) { return p6c_trim_impl(P, self, 1, 1); }
static PN p6c_trim_leading(Potion *P, PN cl, PN self) { return p6c_trim_impl(P, self, 1, 0); }
static PN p6c_trim_trailing(Potion *P, PN cl, PN self) { return p6c_trim_impl(P, self, 0, 1); }
static PN p6c_ord_(Potion *P, PN cl, PN self) {
  const unsigned char *s = (const unsigned char *)PN_STR_PTR(self);
  if (!PN_STR_LEN(self)) return PN_NIL;
  if (s[0] < 0x80) return PN_NUM(s[0]);
  if (s[0] < 0xE0) return PN_NUM(((s[0] & 0x1F) << 6) | (s[1] & 0x3F));
  if (s[0] < 0xF0) return PN_NUM(((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F));
  return PN_NUM(((s[0] & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F));
}
static PN p6c_split(Potion *P, PN cl, PN self, PN sep) {
  PN out = PN_TUP0();
  long i = 0, n = (long)PN_STR_LEN(self);
  sep = p6c_str(P, sep);
  if (!PN_STR_LEN(sep)) { /* every character */
    while (i < n) { long e = i + 1; while (e < n && (((unsigned char)PN_STR_PTR(self)[e]) & 0xC0) == 0x80) e++; out = PN_PUSH(out, potion_str2(P, PN_STR_PTR(self) + i, e - i)); i = e; }
    return out;
  }
  for (;;) {
    long r = p6c_find(self, sep, i);
    if (r < 0) { out = PN_PUSH(out, potion_str2(P, PN_STR_PTR(self) + i, n - i)); break; }
    out = PN_PUSH(out, potion_str2(P, PN_STR_PTR(self) + i, r - i));
    i = r + (long)PN_STR_LEN(sep);
  }
  return out;
}
static PN p6c_lines(Potion *P, PN cl, PN self, PN limit) {
  size_t n = PN_STR_LEN(self), i, st = 0;
  long lim = PN_IS_NUM(limit) ? (long)p6c_dbl(limit) : -1, cnt = 0;
  char *b = malloc(n + 1);
  PN out = PN_TUP0();
  if (!b) potion_allocation_error();
  memcpy(b, PN_STR_PTR(self), n);
  for (i = 0; i <= n; i++) {
    if (i == n ? st < n : b[i] == '\n') {
      size_t e = i;
      if (lim >= 0 && cnt >= lim) break;
      if (e > st && b[e - 1] == '\r') e--;
      out = PN_PUSH(out, potion_str2(P, b + st, e - st));
      cnt++; st = i + 1;
    }
  }
  free(b);
  return out;
}
static PN p6c_f_lines(Potion *P, PN cl, PN self, PN s, PN limit) { return p6c_lines(P, cl, p6c_str(P, s), limit); }
static PN p6c_comb(Potion *P, PN cl, PN self) { return p6c_split(P, cl, self, PN_STR0); }
static PN p6c_chop(Potion *P, PN cl, PN self) { long n = p6c_nchars(self); return n ? p6c_substr(P, cl, self, PN_NUM(0), PN_NUM(n - 1)) : self; }
static PN p6c_chomp(Potion *P, PN cl, PN self) { long n = (long)PN_STR_LEN(self); return n && PN_STR_PTR(self)[n - 1] == '\n' ? potion_str2(P, PN_STR_PTR(self), n - 1) : self; }
static PN p6c_repeat(Potion *P, PN cl, PN self, PN count) {
  long k = (long)p6c_dbl(count), i; PN r = PN_STR("");
  for (i = 0; i < k; i++) r = potion_strcat(P, PN_STR_PTR(r), PN_STR_PTR(self));
  return r;
}
static PN p6c_str_succ(Potion *P, PN cl, PN self) { /* "a".succ, "az".succ, "Zz".succ, "a9".succ */
  long n = (long)PN_STR_LEN(self), i;
  char *b;
  PN r;
  if (!n) return self;
  b = malloc(n + 2);
  if (!b) potion_allocation_error();
  memcpy(b, PN_STR_PTR(self), n); b[n] = 0;
  for (i = n - 1; i >= 0; i--) {
    char c = b[i];
    if (c == 'z') b[i] = 'a';
    else if (c == 'Z') b[i] = 'A';
    else if (c == '9') b[i] = '0';
    else if (isalnum((unsigned char)c)) { b[i]++; r = potion_str2(P, b, n); free(b); return r; }
    else { b[i]++; r = potion_str2(P, b, n); free(b); return r; }
  }
  { char first = PN_STR_PTR(self)[0]; /* carried out of the first column: prepend */
    memmove(b + 1, b, n + 1); b[0] = first == 'z' ? 'a' : first == 'Z' ? 'A' : '1';
    r = potion_str2(P, b, n + 1); free(b); return r; }
}

/* ---- lists ---- */
static PN p6c_join(Potion *P, PN cl, PN self, PN sep) {
  PN s = potion_str(P, ""), sp = sep == PN_NIL ? PN_STR("") : p6c_str(P, sep);
  PN_SIZE i;
  for (i = 0; i < PN_TUPLE_LEN(self); i++) {
    if (i) s = potion_strcat(P, PN_STR_PTR(s), PN_STR_PTR(sp));
    s = potion_strcat(P, PN_STR_PTR(s), PN_STR_PTR(p6c_str(P, PN_TUPLE_AT(self, i))));
    sp = sep == PN_NIL ? PN_STR("") : p6c_str(P, sep);
  }
  return s;
}
static PN p6c_tail(Potion *P, PN cl, PN self, PN n) {
  PN out = PN_TUP0(); long k = n == PN_NIL ? 1 : (long)p6c_dbl(n), len = (long)PN_TUPLE_LEN(self), i;
  if (n == PN_NIL) return len ? PN_TUPLE_AT(self, len - 1) : PN_NIL;
  for (i = len - k < 0 ? 0 : len - k; i < len; i++) out = PN_PUSH(out, PN_TUPLE_AT(self, i));
  return out;
}
static PN p6c_head(Potion *P, PN cl, PN self, PN n) {
  PN out = PN_TUP0(); long k = n == PN_NIL ? 1 : (long)p6c_dbl(n), len = (long)PN_TUPLE_LEN(self), i;
  if (n == PN_NIL) return len ? PN_TUPLE_AT(self, 0) : PN_NIL;
  for (i = 0; i < k && i < len; i++) out = PN_PUSH(out, PN_TUPLE_AT(self, i));
  return out;
}
static PN p6c_last(Potion *P, PN cl, PN self) { return PN_TUPLE_LEN(self) ? PN_TUPLE_AT(self, PN_TUPLE_LEN(self) - 1) : PN_NIL; }
static PN p6c_rotate(Potion *P, PN cl, PN self, PN n) {
  PN out = PN_TUP0(); long len = (long)PN_TUPLE_LEN(self), k = n == PN_NIL ? 1 : (long)p6c_dbl(n), i;
  if (!len) return out;
  k = ((k % len) + len) % len;
  for (i = 0; i < len; i++) out = PN_PUSH(out, PN_TUPLE_AT(self, (i + k) % len));
  return out;
}
static PN p6c_reduce(Potion *P, PN cl, PN self, PN block) {
  PN acc; PN_SIZE i;
  if (!PN_TUPLE_LEN(self)) return PN_NIL;
  acc = PN_TUPLE_AT(self, 0);
  for (i = 1; i < PN_TUPLE_LEN(self); i++) { PN argv[3] = { P->lobby, acc, PN_TUPLE_AT(self, i) }; acc = potion_call(P, block, 3, argv); }
  return acc;
}
/* sort: without a block the elements compare as numbers when both are numbers, else as strings; a block of two
 * parameters is a comparator (<0 0 >0), a block of one parameter maps the element to the sort key */
static int p6c_cmp_default(Potion *P, PN a, PN b) {
  char ba[64], bb[64];
  if (PN_IS_NUM(a) && PN_IS_NUM(b)) { double x = PN_DBL(a), y = PN_DBL(b); return x < y ? -1 : x > y ? 1 : 0; }
  { int c = strcmp(p6t_str(P, a, ba, sizeof ba), p6t_str(P, b, bb, sizeof bb)); return c < 0 ? -1 : c > 0 ? 1 : 0; }
}
static int p6c_sort_cmp(Potion *P, PN block, int nparams, PN a, PN b) {
  if (block == PN_NIL) return p6c_cmp_default(P, a, b);
  if (nparams >= 2) { PN argv[3] = { P->lobby, a, b }; PN r = potion_call(P, block, 3, argv); return p6c_dbl(r) < 0 ? -1 : p6c_dbl(r) > 0 ? 1 : 0; }
  { PN ka = p6_call1(P, block, a), kb = p6_call1(P, block, b); return p6c_cmp_default(P, ka, kb); }
}
static PN p6c_sort(Potion *P, PN cl, PN self, PN block) {
  long n = (long)PN_TUPLE_LEN(self), i, j;
  int np = 0;
  PN out = PN_TUP0();
  if (block != PN_NIL && PN_IS_CLOSURE(block)) {
    PN sig = PN_CLOSURE(block)->sig;
    if (PN_IS_TUPLE(sig)) { PN_SIZE k; for (k = 0; k < PN_TUPLE_LEN(sig); k++) if (PN_IS_STR(PN_TUPLE_AT(sig, k))) np++; }
    if (np == 0) np = 2; /* a block without a signature compares with $a and $b */
  }
  for (i = 0; i < n; i++) out = PN_PUSH(out, PN_TUPLE_AT(self, i));
  for (i = 1; i < n; i++) { /* a stable insertion sort */
    PN v = PN_TUPLE_AT(out, i);
    for (j = i - 1; j >= 0 && p6c_sort_cmp(P, block, np, PN_TUPLE_AT(out, j), v) > 0; j--) PN_TUPLE_AT(out, j + 1) = PN_TUPLE_AT(out, j);
    PN_TUPLE_AT(out, j + 1) = v;
  }
  return out;
}
static PN p6c_list_reverse(Potion *P, PN cl, PN self) { PN out = PN_TUP0(); long i; for (i = (long)PN_TUPLE_LEN(self) - 1; i >= 0; i--) out = PN_PUSH(out, PN_TUPLE_AT(self, i)); return out; }
static PN p6c_list_item(Potion *P, PN cl, PN self) { return self; }
static PN p6c_list_bool(Potion *P, PN cl, PN self) { return PN_TUPLE_LEN(self) ? PN_TRUE : PN_FALSE; }

/* ---- functions ---- */
static PN p6c_f_defined(Potion *P, PN cl, PN self, PN a) { return a == PN_NIL ? PN_FALSE : PN_TRUE; }
static PN p6c_f_abs(Potion *P, PN cl, PN self, PN a) { return p6c_abs(P, cl, a); }
static PN p6c_f_floor(Potion *P, PN cl, PN self, PN a) { return p6c_floor(P, cl, a); }
static PN p6c_f_ceiling(Potion *P, PN cl, PN self, PN a) { return p6c_ceiling(P, cl, a); }
static PN p6c_f_round(Potion *P, PN cl, PN self, PN a) { return p6c_round(P, cl, a); }
static PN p6c_f_chr(Potion *P, PN cl, PN self, PN a) { return p6c_chr_(P, cl, a); }
static PN p6c_f_ord(Potion *P, PN cl, PN self, PN a) { return p6c_ord_(P, cl, p6c_str(P, a)); }
static PN p6c_f_uc(Potion *P, PN cl, PN self, PN a) { return p6c_uc(P, cl, a); }
static PN p6c_f_lc(Potion *P, PN cl, PN self, PN a) { return p6c_lc(P, cl, a); }
static PN p6c_f_ucfirst(Potion *P, PN cl, PN self, PN a) { return p6c_ucfirst(P, cl, a); }
static PN p6c_f_lcfirst(Potion *P, PN cl, PN self, PN a) { return p6c_lcfirst(P, cl, a); }
static PN p6c_f_chars(Potion *P, PN cl, PN self, PN a) { return PN_NUM(p6c_nchars(p6c_str(P, a))); }
static PN p6c_f_flip(Potion *P, PN cl, PN self, PN a) { return p6c_flip(P, cl, p6c_str(P, a)); }
static PN p6c_f_substr(Potion *P, PN cl, PN self, PN a, PN f, PN l) { return p6c_substr(P, cl, p6c_str(P, a), f, l); }
static PN p6c_f_index(Potion *P, PN cl, PN self, PN a, PN n, PN pos) { return p6c_index(P, cl, p6c_str(P, a), n, pos); }
static PN p6c_f_join(Potion *P, PN cl, PN self, PN sep, PN a, PN b, PN c, PN d, PN e) {
  PN all = PN_TUP0(), xs[5] = { a, b, c, d, e }; int i;
  for (i = 0; i < 5; i++) {
    if (xs[i] == PN_P5NOARG) break;
    if (PN_IS_TUPLE(xs[i])) { PN_SIZE j; for (j = 0; j < PN_TUPLE_LEN(xs[i]); j++) all = PN_PUSH(all, PN_TUPLE_AT(xs[i], j)); }
    else if (i == 0 || xs[i] != PN_NIL) all = PN_PUSH(all, xs[i]);
  }
  return p6c_join(P, cl, all, sep);
}
static PN p6c_f_minmax(Potion *P, int want_max, PN a, PN b, PN c) {
  PN xs[3] = { a, b, c }, best = PN_NIL; int i;
  for (i = 0; i < 3; i++) {
    PN v = xs[i];
    if (v == PN_NIL && i) continue;
    if (PN_IS_TUPLE(v)) { PN_SIZE j; for (j = 0; j < PN_TUPLE_LEN(v); j++) { PN w = PN_TUPLE_AT(v, j); if (best == PN_NIL || (want_max ? p6c_dbl(w) > p6c_dbl(best) : p6c_dbl(w) < p6c_dbl(best))) best = w; } }
    else if (best == PN_NIL || (want_max ? p6c_dbl(v) > p6c_dbl(best) : p6c_dbl(v) < p6c_dbl(best))) best = v;
  }
  return best;
}
static PN p6c_f_min(Potion *P, PN cl, PN self, PN a, PN b, PN c) { return p6c_f_minmax(P, 0, a, b, c); }
static PN p6c_f_max(Potion *P, PN cl, PN self, PN a, PN b, PN c) { return p6c_f_minmax(P, 1, a, b, c); }
static PN p6c_f_gcd(Potion *P, PN cl, PN self, PN a, PN b) { return p6c_gcd_(P, cl, a, b); }
static PN p6c_f_lcm(Potion *P, PN cl, PN self, PN a, PN b) { return p6c_lcm_(P, cl, a, b); }
static PN p6c_f_sqrt(Potion *P, PN cl, PN self, PN a) { return p6c_sqrt_(P, cl, a); }
static PN p6c_f_exp(Potion *P, PN cl, PN self, PN a) { return p6c_exp_(P, cl, a); }
static PN p6c_f_log(Potion *P, PN cl, PN self, PN a) { return p6c_log_(P, cl, a); }
static PN p6c_f_sin(Potion *P, PN cl, PN self, PN a) { return p6c_sin_(P, cl, a); }
static PN p6c_f_cos(Potion *P, PN cl, PN self, PN a) { return p6c_cos_(P, cl, a); }
static PN p6c_f_time(Potion *P, PN cl, PN self) { return PN_NUM((long)time(NULL)); }
static PN p6c_f_rand(Potion *P, PN cl, PN self) { return potion_double(P, (double)rand() / ((double)RAND_MAX + 1)); }
static PN p6c_f_reverse(Potion *P, PN cl, PN self, PN a) { return PN_IS_TUPLE(a) ? p6c_list_reverse(P, cl, a) : p6c_flip(P, cl, p6c_str(P, a)); }

static void p6c_init(Potion *P) {
  PN nv[4] = { PN_VTABLE(PN_TNUMBER), PN_VTABLE(PN_TINTEGER), PN_VTABLE(PN_TDOUBLE), PN_VTABLE(PN_TSTRING) };
  int i;
  for (i = 0; i < 3; i++) {
    potion_method(nv[i], "succ", p6c_succ, 0);
    potion_method(nv[i], "pred", p6c_pred, 0);
    potion_method(nv[i], "abs", p6c_abs, 0);
    potion_method(nv[i], "floor", p6c_floor, 0);
    potion_method(nv[i], "ceiling", p6c_ceiling, 0);
    potion_method(nv[i], "round", p6c_round, 0);
    potion_method(nv[i], "truncate", p6c_truncate, 0);
    potion_method(nv[i], "sqrt", p6c_sqrt_, 0);
    potion_method(nv[i], "exp", p6c_exp_, 0);
    potion_method(nv[i], "log", p6c_log_, 0);
    potion_method(nv[i], "sin", p6c_sin_, 0);
    potion_method(nv[i], "cos", p6c_cos_, 0);
    potion_method(nv[i], "tan", p6c_tan_, 0);
    potion_method(nv[i], "asin", p6c_asin_, 0);
    potion_method(nv[i], "acos", p6c_acos_, 0);
    potion_method(nv[i], "atan", p6c_atan_, 0);
    potion_method(nv[i], "sinh", p6c_sinh_, 0);
    potion_method(nv[i], "cosh", p6c_cosh_, 0);
    potion_method(nv[i], "tanh", p6c_tanh_, 0);
    potion_method(nv[i], "asinh", p6c_asinh_, 0);
    potion_method(nv[i], "acosh", p6c_acosh_, 0);
    potion_method(nv[i], "atanh", p6c_atanh_, 0);
    potion_method(nv[i], "log10", p6c_log10_, 0);
    potion_method(nv[i], "log2", p6c_log2_, 0);
    potion_method(nv[i], "cotan", p6c_cotan_, 0);
    potion_method(nv[i], "sec", p6c_sec_, 0);
    potion_method(nv[i], "cosec", p6c_cosec_, 0);
    potion_method(nv[i], "sign", p6c_sign, 0);
    potion_method(nv[i], "gcd", p6c_gcd_, "o=o");
    potion_method(nv[i], "lcm", p6c_lcm_, "o=o");
    potion_method(nv[i], "chr", p6c_chr_, 0);
    potion_method(nv[i], "is-prime", p6c_is_prime, 0);
  }
  { PN sv = nv[3];
    potion_method(sv, "substr", p6c_substr, "from=o|len=o");
    potion_method(sv, "index", p6c_index, "needle=o|pos=o");
    potion_method(sv, "rindex", p6c_rindex, "needle=o");
    potion_method(sv, "contains", p6c_contains, "needle=o");
    potion_method(sv, "starts-with", p6c_starts_with, "n=o");
    potion_method(sv, "ends-with", p6c_ends_with, "n=o");
    potion_method(sv, "uc", p6c_uc, 0);
    potion_method(sv, "lc", p6c_lc, 0);
    potion_method(sv, "ucfirst", p6c_ucfirst, 0);
    potion_method(sv, "lcfirst", p6c_lcfirst, 0);
    potion_method(sv, "tclc", p6c_ucfirst, 0);
    potion_method(sv, "flip", p6c_flip, 0);
    potion_method(sv, "trim", p6c_trim, 0);
    potion_method(sv, "trim-leading", p6c_trim_leading, 0);
    potion_method(sv, "trim-trailing", p6c_trim_trailing, 0);
    potion_method(sv, "ord", p6c_ord_, 0);
    potion_method(sv, "split", p6c_split, "sep=o");
    potion_method(sv, "comb", p6c_comb, 0);
    potion_method(sv, "lines", p6c_lines, "n=o");
    potion_method(sv, "chop", p6c_chop, 0);
    potion_method(sv, "chomp", p6c_chomp, 0);
    potion_method(sv, "succ", p6c_str_succ, 0);
  }
  { PN tv = PN_VTABLE(PN_TTUPLE);
    potion_method(tv, "join", p6c_join, "|sep=o");
    potion_method(tv, "tail", p6c_tail, "|n=o");
    potion_method(tv, "head", p6c_head, "|n=o");
    potion_method(tv, "last", p6c_last, 0);
    potion_method(tv, "rotate", p6c_rotate, "|n=o");
    potion_method(tv, "reduce", p6c_reduce, "block=o");
    potion_method(tv, "reverse", p6c_list_reverse, 0);
    potion_method(tv, "sort", p6c_sort, "|block=o");
    potion_method(tv, "item", p6c_list_item, 0);
    potion_method(tv, "list", p6c_list_item, 0);
    potion_method(tv, "Bool", p6c_list_bool, 0);
  }
  potion_method(P->lobby, "defined", p6c_f_defined, "a=o");
  potion_method(P->lobby, "lines", p6c_f_lines, "a=o,n=o");
  potion_method(P->lobby, "abs", p6c_f_abs, "a=o");
  potion_method(P->lobby, "floor", p6c_f_floor, "a=o");
  potion_method(P->lobby, "ceiling", p6c_f_ceiling, "a=o");
  potion_method(P->lobby, "round", p6c_f_round, "a=o");
  potion_method(P->lobby, "chr", p6c_f_chr, "a=o");
  potion_method(P->lobby, "ord", p6c_f_ord, "a=o");
  potion_method(P->lobby, "uc", p6c_f_uc, "a=o");
  potion_method(P->lobby, "lc", p6c_f_lc, "a=o");
  potion_method(P->lobby, "ucfirst", p6c_f_ucfirst, "a=o");
  potion_method(P->lobby, "lcfirst", p6c_f_lcfirst, "a=o");
  potion_method(P->lobby, "chars", p6c_f_chars, "a=o");
  potion_method(P->lobby, "flip", p6c_f_flip, "a=o");
  potion_method(P->lobby, "substr", p6c_f_substr, "a=o,from=o|len=o");
  potion_method(P->lobby, "index", p6c_f_index, "a=o,n=o|pos=o");
  potion_method(P->lobby, "join", p6c_f_join, "sep=o|a=o,b=o,c=o,d=o,e=o");
  potion_method(P->lobby, "min", p6c_f_min, "a=o|b=o,c=o");
  potion_method(P->lobby, "max", p6c_f_max, "a=o|b=o,c=o");
  potion_method(P->lobby, "gcd", p6c_f_gcd, "a=o,b=o");
  potion_method(P->lobby, "lcm", p6c_f_lcm, "a=o,b=o");
  potion_method(P->lobby, "sqrt", p6c_f_sqrt, "a=o");
  potion_method(P->lobby, "exp", p6c_f_exp, "a=o");
  potion_method(P->lobby, "log", p6c_f_log, "a=o");
  potion_method(P->lobby, "sin", p6c_f_sin, "a=o");
  potion_method(P->lobby, "tan", p6c_f_tan, "a=o");
  potion_method(P->lobby, "asin", p6c_f_asin, "a=o");
  potion_method(P->lobby, "acos", p6c_f_acos, "a=o");
  potion_method(P->lobby, "atan", p6c_f_atan, "a=o");
  potion_method(P->lobby, "sinh", p6c_f_sinh, "a=o");
  potion_method(P->lobby, "cosh", p6c_f_cosh, "a=o");
  potion_method(P->lobby, "tanh", p6c_f_tanh, "a=o");
  potion_method(P->lobby, "asinh", p6c_f_asinh, "a=o");
  potion_method(P->lobby, "acosh", p6c_f_acosh, "a=o");
  potion_method(P->lobby, "atanh", p6c_f_atanh, "a=o");
  potion_method(P->lobby, "log10", p6c_f_log10, "a=o");
  potion_method(P->lobby, "log2", p6c_f_log2, "a=o");
  potion_method(P->lobby, "cotan", p6c_f_cotan, "a=o");
  potion_method(P->lobby, "sec", p6c_f_sec, "a=o");
  potion_method(P->lobby, "cosec", p6c_f_cosec, "a=o");
  potion_method(P->lobby, "atan2", p6c_f_atan2, "a=o,b=o");
  potion_method(P->lobby, "truncate", p6c_f_truncate, "a=o");
  potion_method(P->lobby, "cos", p6c_f_cos, "a=o");
  potion_method(P->lobby, "reverse", p6c_f_reverse, "a=o");
  potion_method(P->lobby, "time", p6c_f_time, 0);
  potion_method(P->lobby, "rand", p6c_f_rand, 0);
}
