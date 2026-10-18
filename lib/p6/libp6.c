/**\file lib/p6/libp6.c
  p6 runtime library.

  Called from p6 syntax compiled via syn/pvip_to_pn.c.
  Loaded by core/compile.c when \c use p6 { } is first encountered.

  (c) 2026 perl11.com
*/

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>

#include "potion.h"
#ifdef _WIN32 /* msvcrt has no strndup */
static char *p6_strndup(const char *s, size_t n) {
  size_t len = 0;
  char *r;
  while (len < n && s[len]) len++;
  r = (char *)malloc(len + 1);
  if (r) { memcpy(r, s, len); r[len] = 0; }
  return r;
}
# define strndup p6_strndup
#endif
#include "p2.h"
#include "khash.h"
#include "table.h"

PN potion_tuple_length(Potion *, PN, PN);

/* ------------------------------------------------------------------ */
/* helpers */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_E
#define M_E  2.71828182845904523536
#endif

#define P6_STUB(msg) do { \
  fprintf(stderr, "p6: %s not yet implemented\n", msg); \
  return PN_NIL; \
} while(0)

#define P6_METHOD(name, sig) potion_method(P->lobby, #name, name, sig)
/* defined in p6core.c / p6test.c (included below) */
static double p6c_dbl(PN v);
static PN p6c_num(Potion *P, double d);
static PN p6c_gcd_(Potion *P, PN cl, PN self, PN o);
static PN p6c_lcm_(Potion *P, PN cl, PN self, PN o);
static const char *p6t_str(Potion *P, PN v, char *buf, size_t sz);

/* ------------------------------------------------------------------ */
/* 1. Constants */

PN p6_pi(Potion *P, PN cl, PN self) {
  return potion_double(P, M_PI);
}

PN p6_e(Potion *P, PN cl, PN self) {
  return potion_double(P, M_E);
}


/* ------------------------------------------------------------------ */
/* 2. Runtime values */

PN p6_rand(Potion *P, PN cl, PN self) {
  return potion_rand(P, cl, self);
}

PN p6_now(Potion *P, PN cl, PN self) {
  return potion_double(P, (double)time(NULL));
}

PN p6_time(Potion *P, PN cl, PN self) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return potion_double(P, (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0);
}


/* ------------------------------------------------------------------ */
/* 3. Whatever / Stub */

PN p6_whatever(Potion *P, PN cl, PN self) {
  /* Whatever ("*") is a sentinel object; create a singleton type */
  P6_STUB("Whatever");
}

PN p6_stub(Potion *P, PN cl, PN self) {
  /* Stub ("...") - used in p6 for incomplete code / yada yada */
  P6_STUB("Stub");
}

/* ------------------------------------------------------------------ */
/* 4. Special variables ($*OUT, $*ERR, @*ARGS, %*ENV, ...) */

PN p6_stdout(Potion *P, PN cl, PN self) {
  return PN_NUM(1); /* fd 1 */
}

PN p6_stderr(Potion *P, PN cl, PN self) {
  return PN_NUM(2); /* fd 2 */
}

PN p6_argv(Potion *P, PN cl, PN self) {
  /* @*ARGS - command line args as tuple; read from @ARGV global */
  return potion_send(P->lobby, PN_STR("@ARGV"));
}

PN p6_exename(Potion *P, PN cl, PN self) {
  return potion_send(P->lobby, PN_STR("$^X"));
}

PN p6_inc(Potion *P, PN cl, PN self) {
  /* @*INC - include paths. Return pn_loader_path */
  return pn_filenames;
}

PN p6_env(Potion *P, PN cl, PN self) {
  /* %*ENV - environment hash */
#ifndef _WIN32 /* mingw declares it */
  extern char **environ;
#endif
  PN t = potion_table_empty(P);
  char **e;
  for (e = environ; *e; e++) {
    char *eq = strchr(*e, '=');
    if (eq) {
      PN key = potion_str2(P, *e, eq - *e);
      PN val = potion_str(P, eq + 1);
      potion_table_put(P, PN_NIL, t, key, val);
    }
  }
  return t;
}

PN p6_tmpdir(Potion *P, PN cl, PN self) {
  const char *tmp = getenv("TMPDIR");
  if (!tmp) tmp = "/tmp";
  return potion_str(P, tmp);
}

PN p6_vm(Potion *P, PN cl, PN self) {
  return potion_str(P, "p2");
}

PN p6_os(Potion *P, PN cl, PN self) {
#ifdef __linux__
  return potion_str(P, "linux");
#elif defined(__APPLE__)
  return potion_str(P, "darwin");
#elif defined(_WIN32)
  return potion_str(P, "win32");
#else
  return potion_str(P, "unix");
#endif
}

PN p6_pid(Potion *P, PN cl, PN self) {
  return PN_NUM(getpid());
}

PN p6_cwd(Potion *P, PN cl, PN self) {
  char buf[4096];
  if (getcwd(buf, sizeof(buf)))
    return potion_str(P, buf);
  return PN_NIL;
}

PN p6_perlver(Potion *P, PN cl, PN self) {
  return potion_str(P, "6.p2");
}

PN p6_osver(Potion *P, PN cl, PN self) {
  P6_STUB("$*OSVER");
}


PN p6_routine(Potion *P, PN cl, PN self) {
  P6_STUB("$?ROUTINE");
}

PN p6_package(Potion *P, PN cl, PN self) {
  P6_STUB("$?PACKAGE");
}

PN p6_class(Potion *P, PN cl, PN self) {
  P6_STUB("$?CLASS");
}

PN p6_module(Potion *P, PN cl, PN self) {
  P6_STUB("$?MODULE");
}

/* ------------------------------------------------------------------ */
/* 5. Special regexp variables */

/* $/: the grammar match being handled by an action (or the last Grammar.parse) */
PN p6_match(Potion *P, PN cl, PN self) {
  return potion_send(P->lobby, potion_str(P, "$*MATCH"));
}

PN p6_exception(Potion *P, PN cl, PN self) {
  return potion_send(P->lobby, potion_str(P, "$!"));
}

/* ------------------------------------------------------------------ */
/* 6. String operations */

/* helper: coerce PN to C string for comparison */
static const char *p6_coerce_str(Potion *P, PN v, char *buf, size_t bufsz) {
  if (PN_IS_STR(v)) return PN_STR_PTR(v);
  if (PN_IS_NUM(v)) {
    snprintf(buf, bufsz, "%.15g", PN_DBL(v));
    return buf;
  }
  if (v == PN_TRUE)  return "1";
  if (v == PN_FALSE) return "0";
  if (v == PN_NIL)   return "";
  return "";
}

PN p6_streq(Potion *P, PN cl, PN self, PN a, PN b) {
  char bufa[64], bufb[64];
  return strcmp(p6_coerce_str(P, a, bufa, sizeof(bufa)),
                p6_coerce_str(P, b, bufb, sizeof(bufb))) == 0 ? PN_TRUE : PN_FALSE;
}
PN p6_strne(Potion *P, PN cl, PN self, PN a, PN b) {
  char bufa[64], bufb[64];
  return strcmp(p6_coerce_str(P, a, bufa, sizeof(bufa)),
                p6_coerce_str(P, b, bufb, sizeof(bufb))) != 0 ? PN_TRUE : PN_FALSE;
}

static int p6_str_cmp_val(Potion *P, PN a, PN b) {
  char bufa[64], bufb[64];
  return strcmp(p6_coerce_str(P, a, bufa, sizeof(bufa)),
                p6_coerce_str(P, b, bufb, sizeof(bufb)));
}

PN p6_strlt(Potion *P, PN cl, PN self, PN a, PN b) {
  return p6_str_cmp_val(P, a, b) < 0 ? PN_TRUE : PN_FALSE;
}

PN p6_strle(Potion *P, PN cl, PN self, PN a, PN b) {
  return p6_str_cmp_val(P, a, b) <= 0 ? PN_TRUE : PN_FALSE;
}

PN p6_strgt(Potion *P, PN cl, PN self, PN a, PN b) {
  return p6_str_cmp_val(P, a, b) > 0 ? PN_TRUE : PN_FALSE;
}

PN p6_strge(Potion *P, PN cl, PN self, PN a, PN b) {
  return p6_str_cmp_val(P, a, b) >= 0 ? PN_TRUE : PN_FALSE;
}

/* +$m: a grammar match numifies by its text, everything else is unchanged */
PN p6_matchstr(Potion *P, PN cl, PN self, PN a) {
  if (PN_IS_TABLE(a)) {
    PN t = potion_table_at(P, PN_NIL, a, potion_str(P, "$text"));
    if (PN_IS_STR(t)) return t;
  }
  return a;
}

/* the string value: a grammar match (a table with "$text") is the matched text */
static PN p6_strval(Potion *P, PN a) {
  if (PN_IS_STR(a)) return a;
  if (PN_IS_TABLE(a)) {
    PN t = potion_table_at(P, PN_NIL, a, potion_str(P, "$text"));
    if (PN_IS_STR(t)) return t;
  }
  return potion_send(a, PN_string);
}

PN p6_concat(Potion *P, PN cl, PN self, PN a, PN b) {
  /* ~ operator: string concatenation with coercion */
  a = p6_strval(P, a);
  b = p6_strval(P, b);
  return PN_STRCAT(PN_STR_PTR(a), PN_STR_PTR(b));
}

PN p6_stringify(Potion *P, PN cl, PN self, PN a) {
  return p6_strval(P, a);
}

PN p6_inplace_concat(Potion *P, PN cl, PN self, PN a, PN b) {
  return p6_concat(P, cl, self, a, b);
}

PN p6_repeat(Potion *P, PN cl, PN self, PN a, PN b) {
  /* x operator: string repeat */
  if (!PN_IS_STR(a)) a = potion_send(a, PN_string);
  int n = PN_IS_NUM(b) ? (int)PN_INT(b) : 0;
  if (n <= 0) return potion_str(P, "");
  char *s = PN_STR_PTR(a);
  size_t len = PN_STR_LEN(a);
  char *buf = malloc(len * n + 1);
  if (!buf) potion_allocation_error();
  size_t i;
  for (i = 0; i < (size_t)n; i++)
    memcpy(buf + i * len, s, len);
  buf[len * n] = '\0';
  PN r = potion_str(P, buf);
  free(buf);
  return r;
}

PN p6_inplace_repeat(Potion *P, PN cl, PN self, PN a, PN b) {
  return p6_repeat(P, cl, self, a, b);
}

/* ------------------------------------------------------------------ */
/* 7. Logical / boolean operations */

PN p6_dor(Potion *P, PN cl, PN self, PN a, PN b) {
  /* // defined-or: return a if defined, else b */
  if (a != PN_NIL) return a;
  return b;
}

PN p6_xor(Potion *P, PN cl, PN self, PN a, PN b) {
  /* ^^ logical xor */
  int ta = PN_TEST(a);
  int tb = PN_TEST(b);
  if (ta && !tb) return a;
  if (!ta && tb) return b;
  return PN_FALSE;
}

PN p6_andthen(Potion *P, PN cl, PN self, PN a, PN b) {
  /* andthen: returns b if a is defined, else a (= undefined) */
  if (PN_TEST(a)) return b;
  return a;
}

/* True / False, not true / false */
static PN p6_bool_str(Potion *P, PN cl, PN self) { return PN_STR(self == PN_TRUE ? "True" : "False"); }

PN p6_bool(Potion *P, PN cl, PN self, PN a) {
  /* ? prefix: boolean coercion */
  return PN_TEST(a) ? PN_TRUE : PN_FALSE;
}

PN p6_so(Potion *P, PN cl, PN self, PN a) {
  /* so prefix: boolean coercion (same as ?) */
  return PN_TEST(a) ? PN_TRUE : PN_FALSE;
}

/* ------------------------------------------------------------------ */
/* 8. Numeric operations */

PN p6_intdiv(Potion *P, PN cl, PN self, PN a, PN b) {
  /* div: integer division (floor division for positive, per p6 spec) */
  double da = PN_DBL(a);
  double db = PN_DBL(b);
  if (db == 0.0) { potion_fatal("Division by zero"); return PN_NIL; }
  return PN_NUM(floor(da / db));
}

static long p6_gcd_impl(long a, long b) {
  while (b) { long t = b; b = a % b; a = t; }
  return a < 0 ? -a : a;
}

PN p6_gcd(Potion *P, PN cl, PN self, PN a, PN b) {
  long la = PN_IS_NUM(a) ? (long)PN_DBL(a) : 0;
  long lb = PN_IS_NUM(b) ? (long)PN_DBL(b) : 0;
  return PN_NUM(p6_gcd_impl(la, lb));
}

PN p6_lcm(Potion *P, PN cl, PN self, PN a, PN b) {
  long la = PN_IS_NUM(a) ? (long)PN_DBL(a) : 0;
  long lb = PN_IS_NUM(b) ? (long)PN_DBL(b) : 0;
  if (la == 0 || lb == 0) return PN_ZERO;
  return PN_NUM((la / p6_gcd_impl(la, lb)) * lb);
}

PN p6_divisible(Potion *P, PN cl, PN self, PN a, PN b) {
  double da = PN_DBL(a);
  double db = PN_DBL(b);
  if (db == 0.0) return PN_FALSE;
  return fmod(da, db) == 0.0 ? PN_TRUE : PN_FALSE;
}

PN p6_not_divisible(Potion *P, PN cl, PN self, PN a, PN b) {
  PN r = p6_divisible(P, cl, self, a, b);
  return r == PN_TRUE ? PN_FALSE : PN_TRUE;
}

/* ------------------------------------------------------------------ */
/* 9. Value / reference identity and comparison */

PN p6_val_eq(Potion *P, PN cl, PN self, PN a, PN b) {
  /* =:= value identity */
  return potion_send(a, PN_cmp, b) == PN_ZERO ? PN_TRUE : PN_FALSE;
}

PN p6_ref_eq(Potion *P, PN cl, PN self, PN a, PN b) {
  /* =:= container identity (same object ptr) */
  if (PN_IS_PTR(a) && PN_IS_PTR(b))
    return a == b ? PN_TRUE : PN_FALSE;
  if (PN_IS_NUM(a) && PN_IS_NUM(b))
    return a == b ? PN_TRUE : PN_FALSE;
  return PN_FALSE;
}

PN p6_eqv(Potion *P, PN cl, PN self, PN a, PN b) {
  /* eqv: canonical equivalence */
  return potion_send(a, PN_cmp, b) == PN_ZERO ? PN_TRUE : PN_FALSE;
}

PN p6_leg(Potion *P, PN cl, PN self, PN a, PN b) {
  char bufa[64], bufb[64];
  int c = strcmp(p6_coerce_str(P, a, bufa, sizeof(bufa)),
                 p6_coerce_str(P, b, bufb, sizeof(bufb)));
  if (c == 0) return potion_str(P, "Same");
  if (c < 0)  return potion_str(P, "Less");
  return potion_str(P, "More");
}
/* `cmp`: numbers by value, anything else as strings (the vm cmp reads a number as a string and crashes) */
PN p6_cmp(Potion *P, PN cl, PN self, PN a, PN b) {
  char bufa[64], bufb[64];
  int c;
  if (PN_IS_NUM(a) && PN_IS_NUM(b)) return potion_send(a, PN_cmp, b);
  c = strcmp(p6_coerce_str(P, a, bufa, sizeof(bufa)), p6_coerce_str(P, b, bufb, sizeof(bufb)));
  return PN_NUM(c < 0 ? -1 : c > 0 ? 1 : 0);
}
/* ------------------------------------------------------------------ */
/* 10. Smartmatch */

static PN p6_regex_apply(Potion *, PN, PN, PN);

PN p6_smartmatch(Potion *P, PN cl, PN self, PN a, PN b) {
  /* ~~ smartmatch: table-driven; basic fallback is eqv */
  if (PN_IS_NUM(b)) {
    /* Numeric: match by value (a string only when it is a number) */
    if (PN_IS_STR(a)) {
      char *end;
      double d = strtod(PN_STR_PTR(a), &end);
      if (end == PN_STR_PTR(a) || *end) return PN_FALSE;
      return d == (PN_IS_INT(b) ? (double)PN_INT(b) : PN_DBL(b)) ? PN_TRUE : PN_FALSE;
    }
    if (!PN_IS_NUM(a)) return PN_FALSE;
    return PN_INT(potion_send(a, PN_cmp, b)) == 0 ? PN_TRUE : PN_FALSE;
  }
  if (PN_IS_STR(b)) {
    /* String: do string equality */
    const char *sa = PN_IS_STR(a) ? PN_STR_PTR(a) : "";
    const char *sb = PN_STR_PTR(b);
    return strcmp(sa, sb) == 0 ? PN_TRUE : PN_FALSE;
  }
  if (PN_IS_TUPLE(b)) { /* a range or list: any element matches */
    PN_SIZE i;
    for (i = 0; i < PN_TUPLE_LEN(b); i++)
      if (p6_smartmatch(P, cl, self, a, PN_TUPLE_AT(b, i)) == PN_TRUE) return PN_TRUE;
    return PN_FALSE;
  }
  if (b == PN_TRUE || b == PN_FALSE) {
    /* Bool: truthiness match */
    return (PN_TEST(a) == (b == PN_TRUE)) ? PN_TRUE : PN_FALSE;
  }
  if (PN_IS_CLOSURE(b)) {
    PN r;
    if (PN_CLOSURE(b)->method == (PN_F)p6_regex_apply)
      r = p6_regex_apply(P, b, P->lobby, a);
    else
      r = potion_send(b, PN_call, a);
    return PN_TEST(r) ? PN_TRUE : PN_FALSE;
  }
  return PN_FALSE; /* conservative fallback */
}

PN p6_not_smartmatch(Potion *P, PN cl, PN self, PN a, PN b) {
  PN r = p6_smartmatch(P, cl, self, a, b);
  return r == PN_TRUE ? PN_FALSE : PN_TRUE;
}

/* ------------------------------------------------------------------ */
/* 11. Bitwise operations */

PN p6_bwor(Potion *P, PN cl, PN self, PN a, PN b) {
  long la = PN_IS_NUM(a) ? (long)PN_DBL(a) : 0;
  long lb = PN_IS_NUM(b) ? (long)PN_DBL(b) : 0;
  return PN_NUM(la | lb);
}

PN p6_bwand(Potion *P, PN cl, PN self, PN a, PN b) {
  long la = PN_IS_NUM(a) ? (long)PN_DBL(a) : 0;
  long lb = PN_IS_NUM(b) ? (long)PN_DBL(b) : 0;
  return PN_NUM(la & lb);
}

PN p6_bwxor(Potion *P, PN cl, PN self, PN a, PN b) {
  long la = PN_IS_NUM(a) ? (long)PN_DBL(a) : 0;
  long lb = PN_IS_NUM(b) ? (long)PN_DBL(b) : 0;
  return PN_NUM(la ^ lb);
}

/* ------------------------------------------------------------------ */
/* 12. List and range operations */

PN p6_range(Potion *P, PN cl, PN self, PN a, PN b) {
  /* a .. b range: create a Range object (lazy list for now: eager) */
  long la = PN_IS_NUM(a) ? (long)PN_DBL(a) : 0;
  long lb = PN_IS_NUM(b) ? (long)PN_DBL(b) : 0;
  PN t = PN_TUP0();
  long i;
  for (i = la; i <= lb; i++) PN_PUSH(t, PN_NUM(i)); /* 5 .. 1 is empty */
  return t;
}

/* [op] LIST: the operands are folded with the operator named OP; comparison operators chain */
PN p6_reduce(Potion *P, PN cl, PN self, PN op, PN list) {
  const char *o = PN_IS_STR(op) ? PN_STR_PTR(op) : "+";
  PN_SIZE i, n;
  PN acc;
  if (!PN_IS_TUPLE(list)) list = PN_TUP(list);
  n = PN_TUPLE_LEN(list);
  if (!n) return !strcmp(o, "*") ? PN_NUM(1) : !strcmp(o, "+") || !strcmp(o, "-") ? PN_NUM(0) : !strcmp(o, "~") ? PN_STR("") : PN_NIL;
  if (!strcmp(o, "**")) { /* right associative */
    double r = p6c_dbl(PN_TUPLE_AT(list, n - 1));
    for (i = n - 1; i > 0; i--) r = pow(p6c_dbl(PN_TUPLE_AT(list, i - 1)), r);
    return p6c_num(P, r);
  }
  acc = PN_TUPLE_AT(list, 0);
  for (i = 1; i < n; i++) {
    PN b = PN_TUPLE_AT(list, i);
    double x = p6c_dbl(acc), y = p6c_dbl(b);
    int cmp = 0;
    if (!strcmp(o, "+")) acc = p6c_num(P, x + y);
    else if (!strcmp(o, "-")) acc = p6c_num(P, x - y);
    else if (!strcmp(o, "*")) acc = p6c_num(P, x * y);
    else if (!strcmp(o, "/")) acc = p6c_num(P, y ? x / y : 0);
    else if (!strcmp(o, "**")) acc = p6c_num(P, pow(x, y));
    else if (!strcmp(o, "%")) acc = p6c_num(P, y ? fmod(x, y) : 0);
    else if (!strcmp(o, "~")) { char b1[64], b2[64]; acc = potion_strcat(P, (char*)p6t_str(P, acc, b1, sizeof b1), (char*)p6t_str(P, b, b2, sizeof b2)); }
    else if (!strcmp(o, "min")) acc = y < x ? b : acc;
    else if (!strcmp(o, "max")) acc = y > x ? b : acc;
    else if (!strcmp(o, "gcd")) acc = p6c_gcd_(P, cl, acc, b);
    else if (!strcmp(o, "lcm")) acc = p6c_lcm_(P, cl, acc, b);
    else if (!strcmp(o, "&&") || !strcmp(o, "and")) acc = PN_TEST(acc) ? b : acc;
    else if (!strcmp(o, "||") || !strcmp(o, "or")) acc = PN_TEST(acc) ? acc : b;
    else { /* chaining comparisons */
      char b1[64], b2[64];
      int sc = strcmp(p6t_str(P, acc, b1, sizeof b1), p6t_str(P, b, b2, sizeof b2));
      if (!strcmp(o, "<=")) cmp = x <= y; else if (!strcmp(o, ">=")) cmp = x >= y;
      else if (!strcmp(o, "<")) cmp = x < y; else if (!strcmp(o, ">")) cmp = x > y;
      else if (!strcmp(o, "==")) cmp = x == y; else if (!strcmp(o, "!=")) cmp = x != y;
      else if (!strcmp(o, "eq")) cmp = sc == 0; else if (!strcmp(o, "ne")) cmp = sc != 0;
      else if (!strcmp(o, "lt")) cmp = sc < 0; else if (!strcmp(o, "gt")) cmp = sc > 0;
      else if (!strcmp(o, "le")) cmp = sc <= 0; else if (!strcmp(o, "ge")) cmp = sc >= 0;
      else return PN_NIL;
      if (!cmp) return PN_FALSE;
      acc = b;
      if (i == n - 1) return PN_TRUE;
    }
  }
  if (n == 1 && (!strcmp(o, "<=") || !strcmp(o, ">=") || !strcmp(o, "<") || !strcmp(o, ">") || !strcmp(o, "==") || !strcmp(o, "eq"))) return PN_TRUE;
  return acc;
}

PN p6_chain(Potion *P, PN cl, PN self, PN a, PN b) {
  /* chained comparison: a op b op c with "is looser" chain semantics.
   * Called with all operand/op pairs as tuple args.
   * For now: just return boolean. */
  return PN_TRUE;
}

PN p6_seq(Potion *P, PN cl, PN self, PN a, PN b) {
  /* sequence operator: returns just the right-hand side */
  return b;
}

PN p6_minmax(Potion *P, PN cl, PN self, PN a, PN b) {
  /* minmax: min or max depending on context */
  PN min = potion_send(a, PN_cmp, b) == PN_NUM(1) ? b : a;
  return min;
}

PN p6_zip(Potion *P, PN cl, PN self, PN a, PN b) {
  /* Z infix: the list of pairs, as long as the shorter list */
  PN out = PN_TUP0();
  PN_SIZE i, n;
  if (!PN_IS_TUPLE(a)) a = PN_TUP(a);
  if (!PN_IS_TUPLE(b)) b = PN_TUP(b);
  n = PN_TUPLE_LEN(a) < PN_TUPLE_LEN(b) ? PN_TUPLE_LEN(a) : PN_TUPLE_LEN(b);
  for (i = 0; i < n; i++) out = PN_PUSH(out, PN_PUSH(PN_TUP(PN_TUPLE_AT(a, i)), PN_TUPLE_AT(b, i)));
  return out;
}

/* ------------------------------------------------------------------ */
/* 13. Conversion / context operations */

PN p6_upto(Potion *P, PN cl, PN self, PN a) {
  /* prefix ^: range from 0 to a-1 */
  if (!PN_IS_NUM(a)) P6_STUB("upto on non-numeric");
  long n = (long)PN_DBL(a);
  PN t = PN_TUP0();
  long i;
  for (i = 0; i < n; i++) PN_PUSH(t, PN_NUM(i));
  return t;
}

PN p6_flatten(Potion *P, PN cl, PN self, PN a) {
  /* prefix |: flatten object into arg list */
  return a;
}

PN p6_chr(Potion *P, PN cl, PN self, PN a) {
  /* chr: codepoint to character string */
  if (!PN_IS_NUM(a)) return potion_str(P, "");
  int cp = (int)PN_DBL(a);
  char buf[5] = {0};
  if (cp < 0x80) {
    buf[0] = (char)cp;
  } else if (cp < 0x800) {
    buf[0] = (char)(0xC0 | (cp >> 6));
    buf[1] = (char)(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    buf[0] = (char)(0xE0 | (cp >> 12));
    buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    buf[2] = (char)(0x80 | (cp & 0x3F));
  } else {
    buf[0] = (char)(0xF0 | (cp >> 18));
    buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    buf[3] = (char)(0x80 | (cp & 0x3F));
  }
  return potion_str(P, buf);
}

PN p6_scalar_deref(Potion *P, PN cl, PN self, PN a) {
  return a;
}

PN p6_array_deref(Potion *P, PN cl, PN self, PN a) {
  return a;
}

PN p6_ctx_scalar(Potion *P, PN cl, PN self, PN a) {
  return a;
}

PN p6_ctx_array(Potion *P, PN cl, PN self, PN a) {
  return a;
}

PN p6_ctx_hash(Potion *P, PN cl, PN self, PN a) {
  return a;
}

/* ------------------------------------------------------------------ */
/* 14. Conditional ternary (?? !!) */

PN p6_conditional(Potion *P, PN cl, PN self, PN cond, PN t, PN f) {
  if (PN_TEST(cond)) return t;
  return f;
}

/* ------------------------------------------------------------------ */
/* 15. Pair / indexing */

PN p6_pair(Potion *P, PN cl, PN self, PN key, PN val) {
  /* key => value: create a Pair */
  PN t = PN_TUP0();
  PN_PUSH(t, key);
  PN_PUSH(t, val);
  return t;
}

/* OBJ[IDX]: a list of indexes (a range, 1,2) is a slice */
PN p6_index(Potion *P, PN cl, PN self, PN obj, PN idx) {
  if (PN_IS_TUPLE(idx)) {
    PN out = PN_TUP0();
    PN_SIZE i;
    for (i = 0; i < PN_TUPLE_LEN(idx); i++) out = PN_PUSH(out, p6_index(P, cl, self, obj, PN_TUPLE_AT(idx, i)));
    return out;
  }
  if (PN_IS_TUPLE(obj)) {
    long i = PN_IS_INT(idx) ? PN_INT(idx) : PN_IS_NUM(idx) ? (long)PN_DBL(idx) : 0;
    if (i < 0) i += (long)PN_TUPLE_LEN(obj);
    if (i < 0 || i >= (long)PN_TUPLE_LEN(obj)) return PN_NIL;
    return PN_TUPLE_AT(obj, i);
  }
  return potion_send(obj, PN_STRN("at", 2), idx);
}

PN p6_atpos(Potion *P, PN cl, PN self, PN obj, PN idx) {
  /* @ indexing (positional) */
  if (PN_IS_TUPLE(obj)) {
    long i = PN_IS_NUM(idx) ? (long)PN_DBL(idx) : 0;
    if (i < 0 || i >= (long)PN_TUPLE_LEN(obj)) return PN_NIL;
    return PN_TUPLE_AT(obj, i);
  }
  return potion_send(obj, PN_STRN("at", 2), idx);
}

PN p6_elems(Potion *P, PN cl, PN self, PN obj) {
  /* .elems / number of positional items, for `for` loop desugaring */
  if (PN_IS_TUPLE(obj)) return PN_NUM(PN_TUPLE_LEN(obj));
  if (obj == PN_NIL) return PN_NUM(0);
  return PN_NUM(1);
}

/* slurp(PATH) / PATH.IO.slurp: the contents of a file (undef if it cannot be read) */
static PN p6_slurp(Potion *P, PN cl, PN self, PN path) {
  FILE *fp;
  char *buf;
  long sz;
  PN r;
  if (!PN_IS_STR(path)) path = potion_send(path, PN_string);
  fp = fopen(PN_STR_PTR(path), "rb");
  if (!fp) return PN_NIL;
  fseek(fp, 0, SEEK_END); sz = ftell(fp); fseek(fp, 0, SEEK_SET);
  buf = malloc(sz + 1);
  if (!buf || fread(buf, 1, sz, fp) != (size_t)sz) { fclose(fp); free(buf); return PN_NIL; }
  fclose(fp);
  r = potion_str2(P, buf, sz);
  free(buf);
  return r;
}
/* spurt(PATH, TEXT) / PATH.IO.spurt(TEXT): write (replace) a file, true on success */
static PN p6_spurt(Potion *P, PN cl, PN self, PN path, PN text) {
  FILE *fp;
  PN ok;
  if (!PN_IS_STR(path)) path = potion_send(path, PN_string);
  if (!PN_IS_STR(text)) text = potion_send(text, PN_string);
  fp = fopen(PN_STR_PTR(path), "wb");
  if (!fp) return PN_FALSE;
  ok = PN_BOOL(fwrite(PN_STR_PTR(text), 1, PN_STR_LEN(text), fp) == PN_STR_LEN(text));
  fclose(fp);
  return ok;
}
static PN p6_str_chars(Potion *P, PN cl, PN self) {
  long i, n = 0;
  for (i = 0; i < (long)PN_STR_LEN(self); i++) if ((((unsigned char)PN_STR_PTR(self)[i]) & 0xC0) != 0x80) n++;
  return PN_NUM(n);
}
/* "a\nb\n".lines: the lines without their newline */
static PN p6_str_lines(Potion *P, PN cl, PN self) {
  PN out = PN_TUP0();
  long i, start = 0, len = PN_STR_LEN(self);
  for (i = 0; i <= len; i++) {
    if (i == len || PN_STR_PTR(self)[i] == '\n') {
      if (i < len || i > start) {
        long e = i;
        if (e > start && PN_STR_PTR(self)[e - 1] == '\r') e--;
        out = PN_PUSH(out, potion_str2(P, PN_STR_PTR(self) + start, e - start));
      }
      start = i + 1;
    }
  }
  return out;
}
static PN p6_str_words(Potion *P, PN cl, PN self) {
  PN out = PN_TUP0();
  long i = 0, len = PN_STR_LEN(self);
  while (i < len) {
    long st;
    while (i < len && isspace((unsigned char)PN_STR_PTR(self)[i])) i++;
    st = i;
    while (i < len && !isspace((unsigned char)PN_STR_PTR(self)[i])) i++;
    if (i > st) out = PN_PUSH(out, potion_str2(P, PN_STR_PTR(self) + st, i - st));
  }
  return out;
}
static PN p6_unlink(Potion *P, PN cl, PN self, PN path) {
  if (!PN_IS_STR(path)) path = potion_send(path, PN_string);
  return PN_BOOL(remove(PN_STR_PTR(path)) == 0);
}
/* list methods taking a block: the block is called with each element (it is the topic $_ of a bare block) */
static PN p6_call1(Potion *P, PN block, PN el) {
  PN argv[2] = { P->lobby, el };
  return potion_call(P, block, 2, argv);
}
static PN p6_list_map(Potion *P, PN cl, PN self, PN block) {
  PN out = PN_TUP0();
  PN_SIZE i;
  for (i = 0; i < PN_TUPLE_LEN(self); i++) {
    PN r = p6_call1(P, block, PN_TUPLE_AT(self, i));
    if (PN_IS_TUPLE(r)) { PN_SIZE j; for (j = 0; j < PN_TUPLE_LEN(r); j++) out = PN_PUSH(out, PN_TUPLE_AT(r, j)); }
    else out = PN_PUSH(out, r);
  }
  return out;
}
static PN p6_list_grep(Potion *P, PN cl, PN self, PN block) {
  PN out = PN_TUP0();
  PN_SIZE i;
  for (i = 0; i < PN_TUPLE_LEN(self); i++) {
    PN el = PN_TUPLE_AT(self, i), r = p6_call1(P, block, el);
    if (PN_TEST(r) && r != PN_ZERO && r != PN_STR0) out = PN_PUSH(out, el);
  }
  return out;
}
static double p6_numval(PN v) { return PN_IS_INT(v) ? (double)PN_INT(v) : PN_IS_NUM(v) ? PN_DBL(v) : 0; }
static PN p6_list_sum(Potion *P, PN cl, PN self) {
  double t = 0; int dbl = 0;
  PN_SIZE i;
  for (i = 0; i < PN_TUPLE_LEN(self); i++) { PN v = PN_TUPLE_AT(self, i); t += p6_numval(v); if (!PN_IS_INT(v)) dbl = 1; }
  return dbl ? potion_send(PN_NUM(0), PN_STR("+"), PN_NUM(t)) : PN_NUM((long)t);
}
static PN p6_list_best(PN self, int want_max) {
  PN best = PN_NIL;
  PN_SIZE i;
  for (i = 0; i < PN_TUPLE_LEN(self); i++) {
    PN v = PN_TUPLE_AT(self, i);
    if (best == PN_NIL || (want_max ? p6_numval(v) > p6_numval(best) : p6_numval(v) < p6_numval(best))) best = v;
  }
  return best;
}
static PN p6_list_max(Potion *P, PN cl, PN self) { return p6_list_best(self, 1); }
static PN p6_list_min(Potion *P, PN cl, PN self) { return p6_list_best(self, 0); }
static PN p6_list_unique(Potion *P, PN cl, PN self) {
  PN out = PN_TUP0();
  PN_SIZE i, j;
  for (i = 0; i < PN_TUPLE_LEN(self); i++) {
    PN v = PN_TUPLE_AT(self, i);
    int seen = 0;
    for (j = 0; j < PN_TUPLE_LEN(out); j++) {
      PN w = PN_TUPLE_AT(out, j);
      if (w == v || (PN_IS_STR(v) && PN_IS_STR(w) && PN_STR_LEN(v) == PN_STR_LEN(w) && !memcmp(PN_STR_PTR(v), PN_STR_PTR(w), PN_STR_LEN(v)))) seen = 1;
    }
    if (!seen) out = PN_PUSH(out, v);
  }
  return out;
}
static PN p6_list_flat(Potion *P, PN cl, PN self) {
  PN out = PN_TUP0();
  PN_SIZE i;
  for (i = 0; i < PN_TUPLE_LEN(self); i++) {
    PN v = PN_TUPLE_AT(self, i);
    if (PN_IS_TUPLE(v)) { PN f = p6_list_flat(P, cl, v); PN_SIZE j; for (j = 0; j < PN_TUPLE_LEN(f); j++) out = PN_PUSH(out, PN_TUPLE_AT(f, j)); }
    else out = PN_PUSH(out, v);
  }
  return out;
}

/* .Str .Int .Numeric .Num: the conversions of numbers and strings */
static PN p6_conv_str(Potion *P, PN cl, PN self) { return PN_IS_STR(self) ? self : self == PN_NIL ? PN_STR("") : potion_send(self, PN_string); }
static PN p6_conv_num(Potion *P, PN cl, PN self) {
  double d;
  if (PN_IS_NUM(self)) return self;
  if (!PN_IS_STR(self)) return PN_NUM(0);
  d = strtod(PN_STR_PTR(self), NULL);
  return d == (double)(long)d ? PN_NUM((long)d) : potion_double(P, d);
}
static PN p6_conv_int(Potion *P, PN cl, PN self) {
  PN n = p6_conv_num(P, cl, self);
  return PN_IS_INT(n) ? n : PN_NUM((long)PN_DBL(n));
}
static PN p6_str_io(Potion *P, PN cl, PN self) { return self; }
static PN p6_str_slurp(Potion *P, PN cl, PN self) { return p6_slurp(P, cl, self, self); }
static PN p6_str_spurt(Potion *P, PN cl, PN self, PN text) { return p6_spurt(P, cl, self, self, text); }

/* multi dispatch helpers: was the argument given, is it of the named type */
static PN p6_given(Potion *P, PN cl, PN self, PN v) { return PN_BOOL(v != PN_P5NOARG && v != PN_NIL); /* a method call leaves a missing argument nil */ }
/* named arguments arrive as pairs (key, value) in the slots after the positionals: the value of KEY in the tuple TUP */
static PN p6_named_get(Potion *P, PN cl, PN self, PN key, PN tup, PN dflt) {
  PN_SIZE i;
  if (!PN_IS_TUPLE(tup)) return PN_NIL;
  for (i = 0; i < PN_TUPLE_LEN(tup); i++) {
    PN e = PN_TUPLE_AT(tup, i);
    if (PN_IS_TUPLE(e) && PN_TUPLE_LEN(e) == 2 && PN_IS_STR(PN_TUPLE_AT(e, 0)) && PN_IS_STR(key) && PN_STR_LEN(PN_TUPLE_AT(e, 0)) == PN_STR_LEN(key)
        && !memcmp(PN_STR_PTR(PN_TUPLE_AT(e, 0)), PN_STR_PTR(key), PN_STR_LEN(key)))
      return PN_TUPLE_AT(e, 1);
  }
  /* not given by name: a plain value in the parameter's own slot was passed positionally (BUILD) */
  if (dflt != PN_P5NOARG && !(PN_IS_TUPLE(dflt) && PN_TUPLE_LEN(dflt) == 2 && PN_IS_STR(PN_TUPLE_AT(dflt, 0)))) return dflt;
  return PN_NIL;
}
/* a positional slot holding a named argument of NAMES is not a positional argument */
static PN p6_named_strip(Potion *P, PN cl, PN self, PN v, PN names) {
  PN_SIZE i;
  if (!PN_IS_TUPLE(v) || PN_TUPLE_LEN(v) != 2 || !PN_IS_STR(PN_TUPLE_AT(v, 0)) || !PN_IS_TUPLE(names)) return v;
  for (i = 0; i < PN_TUPLE_LEN(names); i++) {
    PN k = PN_TUPLE_AT(names, i);
    if (PN_STR_LEN(k) == PN_STR_LEN(PN_TUPLE_AT(v, 0)) && !memcmp(PN_STR_PTR(k), PN_STR_PTR(PN_TUPLE_AT(v, 0)), PN_STR_LEN(k))) return PN_P5NOARG;
  }
  return v;
}
/* p6_named(0, key, tuple, slot-value): the value of a named argument; p6_named(1, value, names): strip it from a positional slot */
static PN p6_named(Potion *P, PN cl, PN self, PN mode, PN a, PN b, PN c) {
  if (PN_INT(mode) == 2) { /* *%rest: the pairs of the tuple B whose key is not one of the NAMES A */
    PN t = potion_table_empty(P);
    PN_SIZE i;
    for (i = 0; PN_IS_TUPLE(b) && i < PN_TUPLE_LEN(b); i++) {
      PN e = PN_TUPLE_AT(b, i);
      if (PN_IS_TUPLE(e) && PN_TUPLE_LEN(e) == 2 && PN_IS_STR(PN_TUPLE_AT(e, 0)) && p6_named_strip(P, cl, self, e, a) != PN_P5NOARG)
        potion_table_put(P, PN_NIL, t, PN_TUPLE_AT(e, 0), PN_TUPLE_AT(e, 1));
    }
    return t;
  }
  return PN_INT(mode) == 0 ? p6_named_get(P, cl, self, a, b, c) : p6_named_strip(P, cl, self, a, b);
}
static PN p6_isa(Potion *P, PN cl, PN self, PN v, PN type) {
  const char *t = PN_STR_PTR(type);
  if (v == PN_P5NOARG) return PN_FALSE;
  if (!strcmp(t, "Int")) return PN_BOOL(PN_IS_INT(v));
  if (!strcmp(t, "Num") || !strcmp(t, "Numeric") || !strcmp(t, "Rat")) return PN_BOOL(PN_IS_NUM(v));
  if (!strcmp(t, "Str")) return PN_BOOL(PN_IS_STR(v));
  if (!strcmp(t, "Bool")) return PN_BOOL(v == PN_TRUE || v == PN_FALSE);
  if (!strcmp(t, "Array") || !strcmp(t, "List") || !strcmp(t, "Positional")) return PN_BOOL(PN_IS_TUPLE(v));
  if (!strcmp(t, "Hash") || !strcmp(t, "Associative")) return PN_BOOL(PN_IS_TABLE(v));
  if (!strcmp(t, "Any") || !strcmp(t, "Mu")) return PN_TRUE;
  if (PN_IS_PTR(v)) { PN cls = potion_send(P->lobby, PN_STR(t)); return PN_BOOL(cls != PN_NIL && PN_VTABLE(PN_TYPE(v)) == cls); }
  return PN_FALSE;
}

/* make X inside a grammar action: X becomes the .made of the match being handled */
PN p6_make(Potion *P, PN cl, PN self, PN x) {
  PN m = potion_send(P->lobby, potion_str(P, "$*MATCH"));
  if (PN_IS_TABLE(m)) potion_table_put(P, PN_NIL, m, potion_str(P, "$made"), x);
  return x;
}
static PN p6_from(Potion *P, PN cl, PN self) {
  return PN_IS_TABLE(self) ? potion_table_at(P, PN_NIL, self, potion_str(P, "$from")) : PN_NIL;
}
static PN p6_to(Potion *P, PN cl, PN self) {
  return PN_IS_TABLE(self) ? potion_table_at(P, PN_NIL, self, potion_str(P, "$to")) : PN_NIL;
}
static PN p6_made(Potion *P, PN cl, PN self) {
  return PN_IS_TABLE(self) ? potion_table_at(P, PN_NIL, self, potion_str(P, "$made")) : PN_NIL;
}

/* {k => v, ...}: the pairs (two element lists, other items are skipped) as a table */
PN p6_hash(Potion *P, PN cl, PN self, PN args) {
  PN t = potion_table_empty(P);
  PN_SIZE i;
  for (i = 0; PN_IS_TUPLE(args) && i < PN_TUPLE_LEN(args); i++) {
    PN e = PN_TUPLE_AT(args, i);
    if (PN_IS_TUPLE(e) && PN_TUPLE_LEN(e) == 2) potion_table_put(P, PN_NIL, t, PN_TUPLE_AT(e, 0), PN_TUPLE_AT(e, 1));
  }
  return t;
}

/* %h<k> = v: a table puts, a list of pairs replaces or appends the pair */
PN p6_putkey(Potion *P, PN cl, PN self, PN obj, PN key, PN val) {
  if (PN_IS_TUPLE(key) && PN_TUPLE_LEN(key) == 1) key = PN_TUPLE_AT(key, 0);
  if (PN_IS_TABLE(obj)) { potion_table_put(P, PN_NIL, obj, key, val); return val; }
  if (PN_IS_TUPLE(obj)) {
    PN_SIZE i;
    for (i = 0; i < PN_TUPLE_LEN(obj); i++) {
      PN e = PN_TUPLE_AT(obj, i);
      if (PN_IS_TUPLE(e) && PN_TUPLE_LEN(e) == 2 && PN_IS_STR(PN_TUPLE_AT(e, 0)) && PN_IS_STR(key) &&
          PN_STR_LEN(PN_TUPLE_AT(e, 0)) == PN_STR_LEN(key) && !memcmp(PN_STR_PTR(PN_TUPLE_AT(e, 0)), PN_STR_PTR(key), PN_STR_LEN(key))) {
        PN_TUPLE_AT(e, 1) = val; PN_TOUCH(e); return val;
      }
    }
    potion_tuple_push(P, obj, PN_PUSH(PN_PUSH(PN_TUP0(), key), val));
  }
  return val;
}

PN p6_atkey(Potion *P, PN cl, PN self, PN obj, PN key) {
  /* % indexing (associative); %h<a> is the word list ("a"), a longer list is a slice */
  if (PN_IS_TUPLE(key) && PN_TUPLE_LEN(key) == 1) key = PN_TUPLE_AT(key, 0);
  if (PN_IS_TUPLE(key)) {
    PN out = PN_TUP0();
    PN_SIZE i;
    for (i = 0; i < PN_TUPLE_LEN(key); i++) out = PN_PUSH(out, p6_atkey(P, cl, self, obj, PN_TUPLE_AT(key, i)));
    return out;
  }
  if (PN_IS_TUPLE(obj)) { /* a list of pairs: my %h = (a => 1) */
    PN_SIZE i;
    for (i = 0; i < PN_TUPLE_LEN(obj); i++) {
      PN e = PN_TUPLE_AT(obj, i);
      if (PN_IS_TUPLE(e) && PN_TUPLE_LEN(e) == 2 && PN_IS_STR(PN_TUPLE_AT(e, 0)) && PN_IS_STR(key) &&
          PN_STR_LEN(PN_TUPLE_AT(e, 0)) == PN_STR_LEN(key) && !memcmp(PN_STR_PTR(PN_TUPLE_AT(e, 0)), PN_STR_PTR(key), PN_STR_LEN(key)))
        return PN_TUPLE_AT(e, 1);
    }
    return PN_NIL;
  }
  if (PN_IS_TABLE(obj))
    return potion_table_at(P, PN_NIL, obj, key);
  return potion_send(obj, PN_STRN("at", 2), key);
}

/* ------------------------------------------------------------------ */
/* 16. Die / Redo */

PN p6_die(Potion *P, PN cl, PN self, PN msg) {
  /* the p5 die: caught by an enclosing eval frame (lives-ok, try), else it prints and exits */
  if (!PN_IS_STR(msg)) msg = potion_send(msg, PN_string);
  return potion_send(P->lobby, potion_str(P, "die"), msg);
}

PN p6_redo(Potion *P, PN cl, PN self) {
  P6_STUB("redo");
}

#include "p6test.c"
#include "p6core.c"

/* ---- test functions for roast6 (plan,ok,is,skip,todo) ---- */

/* ------------------------------------------------------------------ */
/* 17. Complex number */

PN p6_complex(Potion *P, PN cl, PN self, PN re, PN im) {
  /* <re+im i>: complex number */
  P6_STUB("Complex");
}

/* ------------------------------------------------------------------ */
/* 18. Phasers BEGIN / END */

PN p6_begin(Potion *P, PN cl, PN self, PN a, PN b) {
  /* BEGIN { ... } phaser - return the phaser block for compile-time */
  return b;
}

PN p6_end(Potion *P, PN cl, PN self, PN a, PN b) {
  /* END { ... } phaser */
  return b;
}

/* ------------------------------------------------------------------ */
/* 19. Regex */

static PN p6_regex_apply(Potion *P, PN cl, PN self, PN subject) {
  return potion_regex_match(P, cl, subject, PN_CLOSURE(cl)->data[0]);
}

PN p6_regexp(Potion *P, PN cl, PN self, PN pat) {
  PN regex = potion_closure_new(P, (PN_F)p6_regex_apply,
                                potion_sig(P, "subject=o"), 1);
  PN_CLOSURE(regex)->data[0] = pat;
  return regex;
}

PN p6_rx_p5(Potion *P, PN cl, PN self, PN pat) {
  return p6_regexp(P, cl, self, pat);
}

/* ------------------------------------------------------------------ */
/* 20. Object system (OO) - stubs for now */

/* copy the methods of the class or role FROM, which CLS does not define itself, into CLS */
static void p6_copy_methods(Potion *P, PN cls, PN from) {
  unsigned k;
  if (!PN_IS_PTR(cls) || !PN_IS_PTR(from) || PN_TYPE(cls) != PN_TVTABLE
      || PN_TYPE(from) != PN_TVTABLE || potion_fwd(cls) == potion_fwd(from)) return;
  for (k = 0; ; k++) {
    /* potion_def_method may allocate and move the table: fetch it again */
    struct PNTable *t = (struct PNTable *)potion_fwd((PN)((struct PNVtable *)potion_fwd(from))->methods);
    PN key, val;
    if (k >= kh_end(t)) break;
    if (!kh_exist(PN, t, k)) continue;
    key = kh_key(PN, t, k); val = kh_val(PN, t, k);
    if (potion_lookup(P, PN_NIL, potion_fwd(cls), key) == PN_NIL)
      potion_def_method(P, PN_NIL, potion_fwd(cls), key, val);
  }
}

/* p6_compose(Class, (classes in C3 order behind Class), (roles)): the Potion class has a single
 * parent chain, so the methods of a multiple `is A is B` hierarchy and of `does R` roles are
 * copied into the class. The own methods win, then the roles (a role method overrides an
 * inherited one), then the first class in the C3 order that has the method. */
PN p6_compose(Potion *P, PN cl, PN self, PN cls, PN mro, PN roles) {
  PN_SIZE i;
  if (PN_IS_TUPLE(roles))
    for (i = 0; i < PN_TUPLE_LEN(roles); i++) p6_copy_methods(P, cls, PN_TUPLE_AT(roles, i));
  if (PN_IS_TUPLE(mro))
    for (i = 0; i < PN_TUPLE_LEN(mro); i++) p6_copy_methods(P, cls, PN_TUPLE_AT(mro, i));
  return cls;
}

static PN p6_mro_tab; /* class (vtable address) => tuple of the classes behind it in the C3 order */

/* p6_mro(Class, (classes behind it)) */
PN p6_mro(Potion *P, PN cl, PN self, PN cls, PN mro) {
  if (PN_IS_PTR(cls) && PN_TYPE(cls) == PN_TVTABLE)
    potion_table_set(P, p6_mro_tab, PN_NUM((long)potion_fwd(cls)), mro);
  return cls;
}

/* call the method closure of a class on RECV with ARGS (a tuple); the missing arguments take the
 * defaults of the signature. A VM closure gets its argument tuple directly: potion_call's vararg
 * heuristic would drop strings. */
static PN p6_invoke(Potion *P, PN closure, PN recv, PN args) {
  PN_SIZE i, n = PN_IS_TUPLE(args) ? PN_TUPLE_LEN(args) : 0;
  vPN(Closure) c = PN_CLOSURE(closure);
  if (potion_closure_is_vm(closure)) {
    PN ary = PN_TUP0();
    PN sig = ((struct PNProto *)c->data[0])->sig;
    for (i = 0; i < (PN_SIZE)c->arity; i++) {
      PN d = PN_NIL, s;
      if (i < n) d = PN_TUPLE_AT(args, i);
      else if (PN_IS_TUPLE(sig) && (s = potion_sig_at(P, sig, (int)i)) != PN_NIL && PN_IS_TUPLE(s))
        d = PN_TUPLE_LEN(s) == 3 ? PN_TUPLE_AT(s, 2) : PN_NIL;
      ary = PN_PUSH(ary, d);
    }
    return potion_vm(P, c->data[0], recv, ary, c->extra - 1, &c->data[1]);
  } else {
    PN argv[16];
    if (n > 14) n = 14;
    argv[0] = recv;
    for (i = 0; i < n; i++) argv[i + 1] = PN_TUPLE_AT(args, i);
    return potion_call(P, closure, n + 1, argv);
  }
}

/* p6_callnext(self, Class, "meth", (args)): callsame, nextsame, callwith and SUPER::meth: the method
 * behind Class in the C3 order of the class of self (a class without registered order: its parent
 * chain) */
PN p6_callnext(Potion *P, PN cl, PN self, PN obj, PN cls, PN name, PN args) {
  PN chain[PN_C3_MAX], vt = PN_VTABLE(PN_TYPE(obj)), mro;
  int n = 0, i, start = 1;
  chain[n++] = vt;
  mro = potion_table_at(P, PN_NIL, p6_mro_tab, PN_NUM((long)potion_fwd(vt)));
  if (PN_IS_TUPLE(mro)) {
    for (i = 0; i < (int)PN_TUPLE_LEN(mro) && n < PN_C3_MAX; i++) chain[n++] = PN_TUPLE_AT(mro, i);
  } else {
    PNType t = ((struct PNVtable *)potion_fwd(vt))->parent;
    while (t && n < PN_C3_MAX && t != PN_TOBJECT) {
      vt = PN_VTABLE(t);
      chain[n++] = vt;
      t = ((struct PNVtable *)potion_fwd(vt))->parent;
    }
  }
  for (i = 0; i < n; i++)
    if (PN_IS_PTR(cls) && potion_fwd(chain[i]) == potion_fwd(cls)) { start = i + 1; break; }
  for (i = start; i < n; i++) {
    PN m = potion_lookup(P, PN_NIL, potion_fwd(chain[i]), name);
    if (PN_IS_CLOSURE(m)) return p6_invoke(P, m, obj, args);
  }
  return PN_NIL;
}

/* p6_calldirect(obj, Class, "meth", (args)): obj.Class::meth(args) */
PN p6_calldirect(Potion *P, PN cl, PN self, PN obj, PN cls, PN name, PN args) {
  PN vt = cls;
  while (PN_IS_PTR(vt) && PN_TYPE(vt) == PN_TVTABLE) {
    PN m = potion_lookup(P, PN_NIL, potion_fwd(vt), name);
    PNType t;
    if (PN_IS_CLOSURE(m)) return p6_invoke(P, m, obj, args);
    t = ((struct PNVtable *)potion_fwd(vt))->parent;
    if (!t || t == PN_TOBJECT) break;
    vt = PN_VTABLE(t);
  }
  return PN_NIL;
}

PN p6_is(Potion *P, PN cl, PN self, PN a, PN b) {
  /* is trait application */
  return a;
}

PN p6_does(Potion *P, PN cl, PN self, PN a, PN b) {
  return a;
}

PN p6_has(Potion *P, PN cl, PN self, PN a, PN b) {
  P6_STUB("has");
}

PN p6_submethod(Potion *P, PN cl, PN self, PN a, PN b) {
  P6_STUB("submethod");
}

PN p6_multi(Potion *P, PN cl, PN self, PN a, PN b) {
  P6_STUB("multi");
}

PN p6_augment(Potion *P, PN cl, PN self, PN a, PN b) {
  P6_STUB("augment");
}

PN p6_export(Potion *P, PN cl, PN self, PN a, PN b) {
  P6_STUB("export");
}

PN p6_is_copy(Potion *P, PN cl, PN self, PN a, PN b) {
  return a;
}

PN p6_is_rw(Potion *P, PN cl, PN self, PN a, PN b) {
  return a;
}

PN p6_is_ref(Potion *P, PN cl, PN self, PN a, PN b) {
  return a;
}

PN p6_keep(Potion *P, PN cl, PN self, PN a, PN b) {
  return a;
}

PN p6_undo(Potion *P, PN cl, PN self, PN a, PN b) {
  P6_STUB("undo");
}

PN p6_need(Potion *P, PN cl, PN self, PN a, PN b) {
  P6_STUB("need");
}

PN p6_our(Potion *P, PN cl, PN self, PN a, PN b) {
  P6_STUB("our");
}

PN p6_slang(Potion *P, PN cl, PN self, PN a, PN b) {
  P6_STUB("slang");
}

PN p6_path(Potion *P, PN cl, PN self, PN a, PN b) {
  P6_STUB("path");
}

PN p6_enum(Potion *P, PN cl, PN self, PN a, PN b) {
  P6_STUB("enum");
}

/* ------------------------------------------------------------------ */
/* 21. Control flow / binding */

PN p6_try(Potion *P, PN cl, PN self, PN a, PN b) {
  /* try BLOCK: the value of the block, or undef with $! set when it died */
  PN r, err;
  if (!PN_IS_CLOSURE(a)) return a;
  potion_define_global(P, PN_STR("$@"), PN_STR(""));
  r = potion_send(P->lobby, potion_str(P, "p5eval"), a);
  err = potion_send(P->lobby, potion_str(P, "$@"));
  if (PN_IS_STR(err) && PN_STR_LEN(err)) { potion_define_global(P, PN_STR("$!"), err); return PN_NIL; }
  potion_define_global(P, PN_STR("$!"), PN_NIL);
  return r;
}

PN p6_ref(Potion *P, PN cl, PN self, PN a) {
  return a;
}

PN p6_bind(Potion *P, PN cl, PN self, PN a, PN b) {
  return b;
}

PN p6_bind_ro(Potion *P, PN cl, PN self, PN a, PN b) {
  return b;
}

/* ------------------------------------------------------------------ */
/* 22. Metaprogramming / introspection */

PN p6_meta(Potion *P, PN cl, PN self, PN a, PN b) {
  P6_STUB("meta method call");
}

PN p6_attr(Potion *P, PN cl, PN self, PN a, PN b) {
  P6_STUB("attribute");
}

PN p6_funcref(Potion *P, PN cl, PN self, PN a) {
  return a;
}

PN p6_vargs(Potion *P, PN cl, PN self, PN a) {
  return a;
}

PN p6_param(Potion *P, PN cl, PN self, PN a, PN b) {
  P6_STUB("param");
}

/* ------------------------------------------------------------------ */
/* 23. Junctions - stubs for now */

PN p6_junc_and(Potion *P, PN cl, PN self, PN a, PN b) {
  P6_STUB("junction &");
}

PN p6_junc_or(Potion *P, PN cl, PN self, PN a, PN b) {
  P6_STUB("junction |");
}

PN p6_junc_sand(Potion *P, PN cl, PN self, PN a, PN b) {
  P6_STUB("junction & (short-circuit)");
}

/* ================================================================== */
/* Registration */

/* ------------------------------------------------------------------ */
/* Runtime Raku Grammar -> greg compiler.
 *
 * Grammar declarations are lowered by pvip_to_pn into triples
 * (declarator, name, Raku-regex body). This translation deliberately
 * accepts a small, explicit regex subset; unsupported assertions fail at
 * declaration time rather than silently changing the language accepted. */

typedef struct {
  char *s;
  size_t len, cap;
} p6_greg_buf;

static int p6_greg_reserve(p6_greg_buf *b, size_t extra) {
  size_t cap;
  char *s;
  if (b->len + extra + 1 <= b->cap) return 1;
  cap = b->cap ? b->cap : 64;
  while (cap < b->len + extra + 1) cap *= 2;
  s = (char *)realloc(b->s, cap);
  if (!s) return 0;
  b->s = s; b->cap = cap;
  return 1;
}

static int p6_greg_putn(p6_greg_buf *b, const char *s, size_t n) {
  if (!p6_greg_reserve(b, n)) return 0;
  memcpy(b->s + b->len, s, n);
  b->len += n; b->s[b->len] = 0;
  return 1;
}

static int p6_greg_putc(p6_greg_buf *b, char c) {
  return p6_greg_putn(b, &c, 1);
}

static int p6_greg_name(const char *s, size_t n) {
  size_t i;
  if (!n || !(isalpha((unsigned char)s[0]) || s[0] == '_')) return 0;
  for (i = 1; i < n; i++)
    if (!(isalnum((unsigned char)s[i]) || s[i] == '_' || s[i] == '-'))
      return 0;
  return 1;
}

/* every terminal is a leaf of the match tree: ( < TERMINAL > { gm_leaf(..) } ) */
static int p6_term_open(p6_greg_buf *out) { return p6_greg_putn(out, "( < ", 4); }
static int p6_term_close(p6_greg_buf *out) { return p6_greg_putn(out, " > { gm_leaf(P, yytext, yyleng); } )", 36); }
static int p6_greg_literal(p6_greg_buf *out, const char *s, size_t n) {
  size_t i;
  if (!p6_greg_putc(out, '\'')) return 0;
  for (i = 0; i < n; i++) {
    if ((s[i] == '\\' || s[i] == '\'') && !p6_greg_putc(out, '\\')) return 0;
    if (!p6_greg_putc(out, s[i])) return 0;
  }
  return p6_greg_putc(out, '\'');
}

/* `A ~ B C` (goal matching): A, then C, then the goal B. Rewritten as the sequence `A C B` on the rule text
 * before it is translated; an atom is a quoted literal, <..>, [..], (..), \x or a word, with its quantifier. */
static size_t p6_atom_end(const char *s, size_t i, size_t len) {
  size_t j = i;
  if (s[j] == '\'' || s[j] == '"') {
    char q = s[j++];
    while (j < len && s[j] != q) j += (s[j] == '\\' && j + 1 < len) ? 2 : 1;
    if (j < len) j++;
  } else if (s[j] == '<' || s[j] == '[' || s[j] == '(') {
    char open = s[j], close = open == '<' ? '>' : open == '[' ? ']' : ')';
    int depth = 0;
    for (; j < len; j++) {
      if (s[j] == '\'' ) { j++; while (j < len && s[j] != '\'') j++; continue; }
      if (s[j] == open) depth++;
      else if (s[j] == close && --depth == 0) { j++; break; }
    }
  } else if (s[j] == '\\') j += j + 1 < len ? 2 : 1;
  else if (isalnum((unsigned char)s[j]) || s[j] == '_') { while (j < len && (isalnum((unsigned char)s[j]) || s[j] == '_')) j++; }
  else j++;
  while (j < len && (s[j] == '+' || s[j] == '*' || s[j] == '?')) j++;
  return j;
}
static char *p6_goal(const char *src, size_t len) {
  size_t st[256], en[256], n = 0, i = 0, k;
  char *out;
  size_t o = 0;
  int any = 0;
  while (i < len && n < 250) {
    if (isspace((unsigned char)src[i])) { i++; continue; }
    st[n] = i; en[n] = p6_atom_end(src, i, len);
    if (en[n] == i) en[n]++;
    if (src[i] == '~' && en[n] - i == 1) any = 1;
    i = en[n]; n++;
  }
  if (!any) return NULL;
  out = (char *)malloc(len * 2 + 8);
  if (!out) return NULL;
  for (k = 0; k < n; k++) {
    size_t a = st[k], b = en[k];
    if (k > 0 && src[a] == '~' && b - a == 1 && k + 2 < n + 0 && k + 1 < n) {
      /* the previous atom was already copied: B and C follow */
      size_t bs = st[k + 1], be = en[k + 1];
      if (k + 2 < n) { size_t cs = st[k + 2], ce = en[k + 2]; out[o++] = ' '; memcpy(out + o, src + cs, ce - cs); o += ce - cs; out[o++] = ' '; memcpy(out + o, src + bs, be - bs); o += be - bs; k += 2; continue; }
    }
    if (o) out[o++] = ' ';
    memcpy(out + o, src + a, b - a); o += b - a;
  }
  out[o] = 0;
  return out;
}

static char *p6_regex_to_greg(const char *kind, const char *src, size_t len,
                              const char **why) {
  p6_greg_buf out = {0, 0, 0};
  size_t i = 0;
  int sigspace = !strcmp(kind, "rule");
  while (i < len) {
    unsigned char c = (unsigned char)src[i];
    if (isspace(c)) {
      size_t j = i + 1;
      while (j < len && isspace((unsigned char)src[j])) j++;
      if (sigspace && !p6_greg_putn(&out, " ws ", 4)) goto oom;
      i = j; continue;
    }
    if (c == '\'' || c == '"') {
      char quote = (char)c;
      size_t start = i++;
      while (i < len && src[i] != quote) {
        if (src[i] == '\\' && i + 1 < len) i += 2; else i++;
      }
      if (i == len) { *why = "unterminated quoted literal"; goto fail; }
      i++;
      if (!p6_term_open(&out) || !p6_greg_putn(&out, src + start, i - start) || !p6_term_close(&out)) goto oom;
      continue;
    }
    if (c == '<') {
      size_t end = i + 1, start;
      int nocap = 0;
      { int depth = 1;
        while (end < len) { if (src[end] == '<') depth++; else if (src[end] == '>' && --depth == 0) break; end++; } }
      if (end == len) { *why = "unterminated subrule assertion"; goto fail; }
      start = i + 1;
      if (start + 7 < end && (src[start] == '?' || src[start] == '!') && !strncmp(src + start + 1, "before", 6) && isspace((unsigned char)src[start + 7])) {
        /* <?before X> / <!before X>: a lookahead; its leaf actions are dropped with the predicate */
        char *inner = p6_regex_to_greg(kind, src + start + 8, end - start - 8, why);
        int ok;
        if (!inner) goto fail;
        ok = p6_greg_putn(&out, src[start] == '?' ? "&( " : "!( ", 3) && p6_greg_putn(&out, inner, strlen(inner)) && p6_greg_putn(&out, " )", 2);
        free(inner);
        if (!ok) goto oom;
        i = end + 1; continue;
      }
      if (start + 1 < end && (src[start] == '?' || src[start] == '!') && src[start + 1] == '[' && src[end - 1] == ']') {
        /* <?[a..z]> / <![0..9]>: a lookahead on a character class */
        char *inner = p6_regex_to_greg(kind, src + start + 1, end - start - 1, why);
        int ok;
        if (!inner) goto fail;
        ok = p6_greg_putn(&out, src[start] == '?' ? "&( " : "!( ", 3) && p6_greg_putn(&out, inner, strlen(inner)) && p6_greg_putn(&out, " )", 2);
        free(inner);
        if (!ok) goto oom;
        i = end + 1; continue;
      }
      if (start < end && src[start] == '.') { nocap = 1; start++; }
      if (start < end && src[start] == '[' && src[end - 1] == ']') {
        size_t j;
        if (!p6_term_open(&out) || !p6_greg_putc(&out, '[')) goto oom;
        for (j = start + 1; j + 1 < end; j++) {
          if (j + 2 < end && src[j] == '.' && src[j + 1] == '.') {
            if (!p6_greg_putc(&out, '-')) goto oom;
            j++;
          } else if (!p6_greg_putc(&out, src[j])) goto oom;
        }
        if (!p6_greg_putc(&out, ']') || !p6_term_close(&out)) goto oom;
      } else if (p6_greg_name(src + start, end - start)) {
        if (nocap) { /* <.rule>: its text counts, its node is not a submatch */
          if (!p6_greg_putn(&out, "( ", 2) || !p6_greg_putn(&out, src + start, end - start) || !p6_greg_putn(&out, " { gm_hide(P); } )", 18)) goto oom;
        } else if (!p6_greg_putn(&out, src + start, end - start) || !p6_greg_putc(&out, ' ')) goto oom;
      } else {
        *why = "unsupported angle assertion"; goto fail;
      }
      i = end + 1; continue;
    }
    if (c == '\\') {
      if (++i == len) { *why = "trailing backslash"; goto fail; }
      if (!p6_term_open(&out)) goto oom;
      switch (src[i]) {
      case 'd': if (!p6_greg_putn(&out, "[0-9]", 5)) goto oom; break;
      case 'w': if (!p6_greg_putn(&out, "[A-Za-z0-9_]", 12)) goto oom; break;
      case 's': if (!p6_greg_putn(&out, "[ \\t\\r\\n]", 8)) goto oom; break;
      case 'h': if (!p6_greg_putn(&out, "[ \\t]", 4)) goto oom; break;
      default: if (!p6_greg_literal(&out, src + i, 1)) goto oom; break;
      }
      if (!p6_term_close(&out)) goto oom;
      i++; continue;
    }
    if (c == '[') c = '(';
    else if (c == ']') c = ')';
    if (c == '|' && i + 1 < len && src[i + 1] == '|') i++;
    if (c == '.') {
      if (!p6_term_open(&out) || !p6_greg_putc(&out, '.') || !p6_term_close(&out)) goto oom;
      i++; continue;
    }
    if (strchr("()*+?|", c)) {
      if (!p6_greg_putc(&out, (char)c)) goto oom;
      i++; continue;
    }
    if (c == '^' || c == '$') { i++; continue; }
    if (c == ':' && (i + 1 == len || src[i + 1] != ':')) { i++; continue; } /* a ratchet marker: matching is already without backtracking into tokens */
    if (c == ':' || c == '~' || c == '{' || c == '}') {
      *why = "unsupported regex assertion or interpolation"; goto fail;
    }
    {
      size_t start = i++;
      while (i < len && !isspace((unsigned char)src[i]) &&
             !strchr("'\"<>\\[]()*+?|.^$:~{}", src[i])) i++;
      if (!p6_term_open(&out) || !p6_greg_literal(&out, src + start, i - start) || !p6_term_close(&out)) goto oom;
    }
  }
  if (!out.s) {
    out.s = strdup("");
    if (!out.s) goto oom;
  }
  return out.s;
oom:
  *why = "out of memory";
fail:
  free(out.s);
  return NULL;
}

PN p6_grammar(Potion *P, PN cl, PN self, PN name, PN rules) {
  static const char header[] =
    "%{\n#include \"p2.h\"\n#include \"internal.h\"\n"
    "#define YYSTYPE PN\n#define YY_XTYPE Potion *\n#define YY_XVAR P\n"
    "#define YY_INPUT(buf, result, max) { Potion *P = G->data; \\\n"
    "  if (P->yypos < PN_STR_LEN(P->input)) { \\\n"
    "    result = max; \\\n"
    "    if (P->yypos + max > PN_STR_LEN(P->input)) result = PN_STR_LEN(P->input) - P->yypos; \\\n"
    "    PN_MEMCPY_N(buf, PN_STR_PTR(P->input) + P->yypos, char, result); \\\n"
    "    if (result < max) ((char *)buf)[result] = 0; \\\n"
    "    P->yypos += max; \\\n"
    "  } else result = 0; }\n"
    "#define YY_NAME(N) p6_user_##N\n"
    "/* the match tree: leaves (strings) and finished rules (tables) pile up on gm_items; a rule\n"
    " * takes everything since its gm_begin as its children. \"$text\" is the matched text, a child\n"
    " * is stored under its rule name (a tuple when the rule matched several times). */\n"
    "static PN gm_items; static long gm_marks[128], gm_starts[128], gm_pos; static int gm_depth, gm_rooted;\n"
    "static void gm_reset(Potion *P) { if (!gm_rooted) { potion_gc_root(P, &gm_items); gm_rooted = 1; } gm_items = PN_TUP0(); gm_depth = 0; gm_pos = 0; }\n"
    "static void gm_begin(Potion *P) { if (gm_depth < 128) { gm_marks[gm_depth] = PN_TUPLE_LEN(gm_items); gm_starts[gm_depth] = gm_pos; } gm_depth++; }\n"
    "static void gm_leaf(Potion *P, const char *t, int n) { gm_items = PN_PUSH(gm_items, PN_STRN(t, n)); gm_pos += n; }\n"
    "static void gm_end(Potion *P, const char *name) {\n"
    "  long mark, n, i, len = 0, o = 0; PN node, rest = PN_TUP0(); char *buf;\n"
    "  gm_depth--; mark = gm_depth < 128 ? gm_marks[gm_depth] : 0; n = PN_TUPLE_LEN(gm_items);\n"
    "  node = potion_table_empty(P); PN key = PN_STR(\"$text\");\n"
    "  for (i = mark; i < n; i++) { PN it = PN_TUPLE_AT(gm_items, i);\n"
    "    len += PN_IS_STR(it) ? PN_STR_LEN(it) : PN_STR_LEN(potion_table_at(P, PN_NIL, it, key)); }\n"
    "  buf = malloc(len + 1);\n"
    "  for (i = mark; i < n; i++) { PN it = PN_TUPLE_AT(gm_items, i), t;\n"
    "    if (PN_IS_STR(it)) t = it; else { t = potion_table_at(P, PN_NIL, it, key);\n"
    "      PN cn = potion_table_at(P, PN_NIL, it, PN_STR(\"$name\"));\n"
    "      if (strcmp(PN_STR_PTR(cn), \"ws\") && potion_table_at(P, PN_NIL, it, PN_STR(\"$hidden\")) == PN_NIL) { PN old = potion_table_at(P, PN_NIL, node, cn);\n"
    "        if (old == PN_NIL) potion_table_put(P, PN_NIL, node, cn, it);\n"
    "        else if (PN_IS_TUPLE(old)) potion_table_put(P, PN_NIL, node, cn, PN_PUSH(old, it));\n"
    "        else potion_table_put(P, PN_NIL, node, cn, PN_PUSH(PN_PUSH(PN_TUP0(), old), it)); } }\n"
    "    memcpy(buf + o, PN_STR_PTR(t), PN_STR_LEN(t)); o += PN_STR_LEN(t); }\n"
    "  potion_table_put(P, PN_NIL, node, key, PN_STRN(buf, o)); free(buf);\n"
    "  potion_table_put(P, PN_NIL, node, PN_STR(\"$name\"), PN_STR(name));\n"
    "  potion_table_put(P, PN_NIL, node, PN_STR(\"$from\"), PN_NUM(gm_depth < 128 ? gm_starts[gm_depth] : 0)); potion_table_put(P, PN_NIL, node, PN_STR(\"$to\"), PN_NUM(gm_pos));\n"
    "  for (i = 0; i < mark; i++) rest = PN_PUSH(rest, PN_TUPLE_AT(gm_items, i));\n"
    "  gm_items = PN_PUSH(rest, node);\n"
    "}\n"
    "static void gm_hide(Potion *P) { long n = PN_TUPLE_LEN(gm_items); if (n && !PN_IS_STR(PN_TUPLE_AT(gm_items, n - 1))) potion_table_put(P, PN_NIL, PN_TUPLE_AT(gm_items, n - 1), PN_STR(\"$hidden\"), PN_TRUE); }\n"
    "static PN gm_result(Potion *P) { long n = PN_TUPLE_LEN(gm_items); return n ? PN_TUPLE_AT(gm_items, n - 1) : PN_TRUE; }\n"
    "%}\n"
    "start = { gm_reset(P); } TOP end-of-file { $$ = P->source = gm_result(P); } | !. substart\n"
    "substart = { gm_reset(P); } TOP { $$ = P->source = gm_result(P); }\n";
  static const char footer[] =
    "end-of-file = !.\n%%\n"
    "PN p6_grammar_parse(Potion *P, PN code, char *filename) {\n"
    "  GREG *G = p6_user_parse_new(P); int oldpos = P->yypos; PN oldin = P->input, oldsrc = P->source, out;\n"
    "  P->yypos = 0; P->input = code; P->source = PN_NIL;\n"
    "  out = (filename && filename[0] == 1 ? p6_user_parse_from(G, yy_substart) : p6_user_parse(G)) ? P->source : PN_FALSE; p6_user_parse_free(G);\n"
    "  P->source = oldsrc; P->yypos = oldpos; P->input = oldin; return out;\n}\n";
  PN out;
  int has_top = 0, has_ws = 0, uses_ws = 0, nsyms = 0;
  char *symbase[64], *symname[64], *base_for_node = NULL;
  long i;
  (void)cl; (void)self;
  if (!PN_IS_STR(name) || !PN_IS_TUPLE(rules))
    return potion_type_error_want(P, "grammar", rules, "String name and Tuple rules");
  out = potion_byte_str(P, (char *)header);
  for (i = 0; i < (long)PN_TUPLE_LEN(rules); i++) {
    PN triple = PN_TUPLE_AT(rules, i);
    PN kind, rule_name, body;
    char *kcopy, *ncopy, *bcopy, *translated;
    const char *why = NULL;
    if (!PN_IS_TUPLE(triple) || PN_TUPLE_LEN(triple) != 3)
      return potion_type_error_want(P, "rule", triple, "(kind,name,body)");
    kind = PN_TUPLE_AT(triple, 0);
    rule_name = PN_TUPLE_AT(triple, 1);
    body = PN_TUPLE_AT(triple, 2);
    if (!PN_IS_STR(kind) || !PN_IS_STR(rule_name) || !PN_IS_STR(body))
      return potion_type_error_want(P, "rule", triple, "String rule fields");
    kcopy = strndup(PN_STR_PTR(kind), PN_STR_LEN(kind));
    ncopy = strndup(PN_STR_PTR(rule_name), PN_STR_LEN(rule_name));
    bcopy = strndup(PN_STR_PTR(body), PN_STR_LEN(body));
    if (!kcopy || !ncopy || !bcopy) potion_allocation_error();
    if (strcmp(kcopy, "token") && strcmp(kcopy, "rule") && strcmp(kcopy, "regex") && strcmp(kcopy, "proto")) {
      free(kcopy); free(ncopy); free(bcopy);
      return potion_error(P, potion_str(P, "Unknown grammar rule declarator"), 0, 0, PN_NIL);
    }
    if (!strcmp(kcopy, "proto")) { free(kcopy); free(ncopy); free(bcopy); continue; }
    { /* op:sym<+> is the alternative `op_sym_N` of the rule op */
      char *sym = strstr(ncopy, ":sym<");
      if (sym) {
        char alt[200];
        snprintf(alt, sizeof alt, "%.*s_sym_%d", (int)(sym - ncopy), ncopy, nsyms);
        symbase[nsyms] = strndup(ncopy, sym - ncopy);
        symname[nsyms++] = strdup(alt);
        base_for_node = symbase[nsyms - 1];
        free(ncopy); ncopy = strdup(alt);
      } else base_for_node = NULL;
    }
    if (!p6_greg_name(ncopy, strlen(ncopy))) {
      PN err = potion_error(P, potion_str_format(P, "Invalid grammar rule name: %s", ncopy), 0, 0, PN_NIL);
      free(kcopy); free(ncopy); free(bcopy); return err;
    }
    { char *g = p6_goal(bcopy, strlen(bcopy)); /* A ~ B C */
      translated = p6_regex_to_greg(kcopy, g ? g : bcopy, strlen(g ? g : bcopy), &why);
      free(g); }
    free(bcopy);
    if (!translated) {
      PN err = potion_error(P, potion_str_format(P, "Unsupported grammar rule %s: %s", ncopy, why), 0, 0, PN_NIL);
      free(kcopy); free(ncopy); return err;
    }
    if (!strcmp(ncopy, "TOP")) has_top = 1;
    if (!strcmp(ncopy, "ws")) has_ws = 1;
    if (!strcmp(kcopy, "rule")) uses_ws = 1;
    pn_printf(P, out, "%s = { gm_begin(P); } %s { gm_end(P, \"%s\"); }\n", ncopy, translated, base_for_node ? base_for_node : ncopy);
    free(kcopy); free(ncopy); free(translated);
  }
  { /* the alternatives of every proto rule: base = base_sym_0 | base_sym_1 ... */
    int a, b;
    for (a = 0; a < nsyms; a++) {
      int first = 1;
      for (b = 0; b < a; b++) if (!strcmp(symbase[a], symbase[b])) first = 0;
      if (!first) continue;
      pn_printf(P, out, "%s = ", symbase[a]);
      for (b = 0, first = 1; b < nsyms; b++)
        if (!strcmp(symbase[a], symbase[b])) { pn_printf(P, out, "%s%s", first ? "" : " | ", symname[b]); first = 0; }
      pn_printf(P, out, "\n");
      if (!strcmp(symbase[a], "TOP")) has_top = 1;
    }
    for (a = 0; a < nsyms; a++) { free(symbase[a]); free(symname[a]); }
  }
  if (!has_top)
    return potion_error(P, potion_str(P, "A grammar requires token, rule, or regex TOP"), 0, 0, PN_NIL);
  if (uses_ws && !has_ws) pn_printf(P, out, "ws = [ \\t\\r\\n]*\n");
  pn_printf(P, out, "%s", footer);
  return potion_syntax_compile(P, name, PN_STR_B(out));
}

DLLEXPORT
void Potion_Init_libp6(Potion *P) {
  /* Grammar is another name of Syntax: potion_define_global would rename the vtable (and leave its name an
   * unrooted young string behind, which crashed the next major gc of big programs) */
  { PN syn = potion_send(P->lobby, PN_STR("Syntax")); potion_send(P->lobby, PN_def, PN_STR("Grammar"), syn); }
  P6_METHOD(p6_grammar,  "name=S,rules=u");
  /* 1. Constants */
  P6_METHOD(p6_pi,       0);
  P6_METHOD(p6_e,        0);
  P6_METHOD(p6_rand,     0);
  P6_METHOD(p6_now,      0);
  P6_METHOD(p6_stub,     0);

  /* 2. Special variables */
  P6_METHOD(p6_stdout,   0);
  P6_METHOD(p6_stderr,   0);
  P6_METHOD(p6_argv,     0);
  P6_METHOD(p6_inc,      0);
  P6_METHOD(p6_env,      0);
  P6_METHOD(p6_tmpdir,   0);
  P6_METHOD(p6_vm,       0);
  P6_METHOD(p6_os,       0);
  P6_METHOD(p6_pid,      0);
  P6_METHOD(p6_cwd,      0);
  P6_METHOD(p6_perlver,  0);
  P6_METHOD(p6_osver,    0);
  P6_METHOD(p6_exename,  0);
  P6_METHOD(p6_routine,  0);
  P6_METHOD(p6_package,  0);
  P6_METHOD(p6_class,    0);
  P6_METHOD(p6_module,   0);
  P6_METHOD(p6_match,    0);
  P6_METHOD(p6_exception,0);

  /* 3. String ops */
  P6_METHOD(p6_streq,     "a=o,b=o");
  P6_METHOD(p6_strne,     "a=o,b=o");
  P6_METHOD(p6_strlt,     "a=o,b=o");
  P6_METHOD(p6_cmp,       "a=o,b=o");
  P6_METHOD(p6_strle,     "a=o,b=o");
  P6_METHOD(p6_strgt,     "a=o,b=o");
  P6_METHOD(p6_strge,     "a=o,b=o");
  P6_METHOD(p6_concat,    "a=o,b=o");
  P6_METHOD(p6_stringify, "a=o");
  P6_METHOD(p6_inplace_concat, "a=o,b=o");
  P6_METHOD(p6_repeat,    "a=o,b=o");
  P6_METHOD(p6_inplace_repeat, "a=o,b=o");

  /* 4. Logical / boolean */
  P6_METHOD(p6_dor,       "a=o,b=o");
  P6_METHOD(p6_xor,       "a=o,b=o");
  P6_METHOD(p6_andthen,   "a=o,b=o");
  P6_METHOD(p6_bool,      "a=o");
  P6_METHOD(p6_so,        "a=o");

  /* 5. Numeric */
  P6_METHOD(p6_intdiv,    "a=o,b=o");
  P6_METHOD(p6_gcd,       "a=o,b=o");
  P6_METHOD(p6_lcm,       "a=o,b=o");
  P6_METHOD(p6_divisible, "a=o,b=o");
  P6_METHOD(p6_not_divisible, "a=o,b=o");

  /* 6. Identity / comparison */
  P6_METHOD(p6_val_eq,    "a=o,b=o");
  P6_METHOD(p6_ref_eq,    "a=o,b=o");
  P6_METHOD(p6_eqv,       "a=o,b=o");
  P6_METHOD(p6_leg,       "a=o,b=o");
  P6_METHOD(p6_smartmatch,"a=o,b=o");
  P6_METHOD(p6_not_smartmatch, "a=o,b=o");

  /* 7. Bitwise */
  P6_METHOD(p6_bwor,      "a=o,b=o");
  P6_METHOD(p6_bwand,     "a=o,b=o");
  P6_METHOD(p6_bwxor,     "a=o,b=o");

  /* 8. List / range */
  P6_METHOD(p6_range,     "a=o,b=o");
  P6_METHOD(p6_reduce,    "a=o,b=o");
  P6_METHOD(p6_chain,     "a=o,b=o");
  P6_METHOD(p6_seq,       "a=o,b=o");
  P6_METHOD(p6_minmax,    "a=o,b=o");
  P6_METHOD(p6_zip,       "a=o,b=o");
  P6_METHOD(p6_upto,      "a=o");
  P6_METHOD(p6_flatten,   "a=o");
  P6_METHOD(p6_elems,     "a=o");
  P6_METHOD(p6_chr,       "a=o");

  /* 9. Context / deref */
  P6_METHOD(p6_scalar_deref, "a=o");
  P6_METHOD(p6_array_deref,  "a=o");
  P6_METHOD(p6_ctx_scalar,   "a=o");
  P6_METHOD(p6_ctx_array,    "a=o");
  P6_METHOD(p6_ctx_hash,     "a=o");

  /* 10. Conditional */
  P6_METHOD(p6_conditional, "a=o,b=o,c=o");

  /* 11. Pair / indexing */
  P6_METHOD(p6_pair,    "a=o,b=o");
  /* @a.elems: the number of elements */
  potion_method(PN_VTABLE(PN_TTUPLE), "elems", potion_tuple_length, 0);
  P6_METHOD(p6_atpos,   "a=o,b=o");
  P6_METHOD(p6_index,   "a=o,b=o");
  P6_METHOD(p6_atkey,   "a=o,b=o");
  P6_METHOD(p6_hash,    "args=u");
  potion_method(P->lobby, "p6_given", p6_given, "v=o");
  potion_method(P->lobby, "p6_named", p6_named, "mode=o,a=o,b=o,c=o");
  potion_method(P->lobby, "p6_isa", p6_isa, "v=o,type=o");
  P6_METHOD(p6_matchstr, "a=o");
  potion_method(P->lobby, "make", p6_make, "x=o");
  potion_method(P->lobby, "slurp", p6_slurp, "path=o");
  potion_method(P->lobby, "spurt", p6_spurt, "path=o,text=o");
  potion_method(P->lobby, "unlink", p6_unlink, "path=o");
  potion_method(PN_VTABLE(PN_TSTRING), "IO", p6_str_io, 0);
  { PN vts[4] = { PN_VTABLE(PN_TSTRING), PN_VTABLE(PN_TNUMBER), PN_VTABLE(PN_TINTEGER), PN_VTABLE(PN_TDOUBLE) }; int i;
    for (i = 0; i < 4; i++) {
      potion_method(vts[i], "Str", p6_conv_str, 0);
      potion_method(vts[i], "Numeric", p6_conv_num, 0);
      potion_method(vts[i], "Num", p6_conv_num, 0);
      potion_method(vts[i], "Int", p6_conv_int, 0);
    } }
  potion_method(PN_VTABLE(PN_TTUPLE), "map", p6_list_map, "block=o");
  potion_method(PN_VTABLE(PN_TTUPLE), "grep", p6_list_grep, "block=o");
  potion_method(PN_VTABLE(PN_TTUPLE), "sum", p6_list_sum, 0);
  potion_method(PN_VTABLE(PN_TTUPLE), "max", p6_list_max, 0);
  potion_method(PN_VTABLE(PN_TTUPLE), "min", p6_list_min, 0);
  potion_method(PN_VTABLE(PN_TTUPLE), "unique", p6_list_unique, 0);
  potion_method(PN_VTABLE(PN_TTUPLE), "flat", p6_list_flat, 0);
  potion_method(PN_VTABLE(PN_TSTRING), "chars", p6_str_chars, 0);
  potion_method(PN_VTABLE(PN_TSTRING), "lines", p6_str_lines, 0);
  potion_method(PN_VTABLE(PN_TSTRING), "words", p6_str_words, 0);
  potion_method(PN_VTABLE(PN_TSTRING), "slurp", p6_str_slurp, 0);
  potion_method(PN_VTABLE(PN_TSTRING), "spurt", p6_str_spurt, "text=o");
  potion_method(PN_VTABLE(PN_TTABLE), "made", p6_made, 0);
  potion_method(PN_VTABLE(PN_TTABLE), "from", p6_from, 0);
  potion_method(PN_VTABLE(PN_TTABLE), "to", p6_to, 0);
  P6_METHOD(p6_putkey,  "a=o,b=o,c=o");

  /* 12. Control flow */
  P6_METHOD(p6_die,     "a=o");
  P6_METHOD(p6_redo,    0);

  /* Test functions (plan,ok,is) */
  p6t_init(P);
  p6c_init(P);
  potion_method(PN_VTABLE(PN_TBOOLEAN), "string", p6_bool_str, 0);
  /* 13. Complex */
  P6_METHOD(p6_complex, "a=o,b=o");

  /* 14. Phasers */
  P6_METHOD(p6_begin,   "a=o,b=o");
  P6_METHOD(p6_end,     "a=o,b=o");

  /* 15. Regex */
  P6_METHOD(p6_regexp,  "a=o");
  P6_METHOD(p6_rx_p5,   "a=o");

  /* 16. OO - stubs */
  P6_METHOD(p6_compose,    "a=o,b=o,c=o");
  P6_METHOD(p6_mro,        "a=o,b=o");
  P6_METHOD(p6_callnext,   "a=o,b=o,c=o,d=o");
  P6_METHOD(p6_calldirect, "a=o,b=o,c=o,d=o");
  p6_mro_tab = potion_table_empty(P); potion_gc_root(P, &p6_mro_tab);
  P6_METHOD(p6_is,         "a=o,b=o");
  P6_METHOD(p6_does,       "a=o,b=o");
  P6_METHOD(p6_has,        "a=o,b=o");
  P6_METHOD(p6_submethod,  "a=o,b=o");
  P6_METHOD(p6_multi,      "a=o,b=o");
  P6_METHOD(p6_augment,    "a=o,b=o");
  P6_METHOD(p6_export,     "a=o,b=o");
  P6_METHOD(p6_is_copy,    "a=o,b=o");
  P6_METHOD(p6_is_rw,      "a=o,b=o");
  P6_METHOD(p6_is_ref,     "a=o,b=o");
  P6_METHOD(p6_keep,       "a=o,b=o");
  P6_METHOD(p6_undo,       "a=o,b=o");
  P6_METHOD(p6_need,       "a=o,b=o");
  P6_METHOD(p6_our,        "a=o,b=o");
  P6_METHOD(p6_slang,      "a=o,b=o");
  P6_METHOD(p6_path,       "a=o,b=o");
  P6_METHOD(p6_enum,       "a=o,b=o");
  P6_METHOD(p6_package,    "a=o,b=o");
  P6_METHOD(p6_module,     "a=o,b=o");

  /* 17. Control / binding */
  P6_METHOD(p6_try,      "a=o,b=o");
  P6_METHOD(p6_ref,      "a=o");
  P6_METHOD(p6_bind,     "a=o,b=o");
  P6_METHOD(p6_bind_ro,  "a=o,b=o");

  /* 18. Metaprogramming */
  P6_METHOD(p6_meta,     "a=o,b=o");
  P6_METHOD(p6_attr,     "a=o,b=o");
  P6_METHOD(p6_funcref,  "a=o");
  P6_METHOD(p6_vargs,    "a=o");
  P6_METHOD(p6_param,    "a=o,b=o");

  /* 19. Junctions */
  P6_METHOD(p6_junc_and,  "a=o,b=o");
  P6_METHOD(p6_junc_or,   "a=o,b=o");
  P6_METHOD(p6_junc_sand, "a=o,b=o");
}
