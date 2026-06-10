/**\file lib/p6/libp6.c
  p6 runtime library.

  Called from p6 syntax compiled via syn/pvip_to_pn.c.
  Loaded by core/compile.c when \c use p6 { } is first encountered.

  (c) 2026 perl11 org
*/

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>

#include "potion.h"
#include "p2.h"

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
  extern char **environ;
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

PN p6_match(Potion *P, PN cl, PN self) {
  P6_STUB("$/");
}

PN p6_exception(Potion *P, PN cl, PN self) {
  P6_STUB("$!");
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

PN p6_concat(Potion *P, PN cl, PN self, PN a, PN b) {
  /* ~ operator: string concatenation with coercion */
  if (!PN_IS_STR(a)) a = potion_send(a, PN_string);
  if (!PN_IS_STR(b)) b = potion_send(b, PN_string);
  return PN_STRCAT(PN_STR_PTR(a), PN_STR_PTR(b));
}

PN p6_stringify(Potion *P, PN cl, PN self, PN a) {
  if (PN_IS_STR(a)) return a;
  return potion_send(a, PN_string);
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
/* ------------------------------------------------------------------ */
/* 10. Smartmatch */

PN p6_smartmatch(Potion *P, PN cl, PN self, PN a, PN b) {
  /* ~~ smartmatch: table-driven; basic fallback is eqv */
  if (PN_IS_NUM(b)) {
    /* Numeric: match by value */
    return PN_INT(potion_send(a, PN_cmp, b)) == 0 ? PN_TRUE : PN_FALSE;
  }
  if (PN_IS_STR(b)) {
    /* String: do string equality */
    const char *sa = PN_IS_STR(a) ? PN_STR_PTR(a) : "";
    const char *sb = PN_STR_PTR(b);
    return strcmp(sa, sb) == 0 ? PN_TRUE : PN_FALSE;
  }
  if (b == PN_TRUE || b == PN_FALSE) {
    /* Bool: truthiness match */
    return (PN_TEST(a) == (b == PN_TRUE)) ? PN_TRUE : PN_FALSE;
  }
  if (PN_IS_CLOSURE(b)) {
    /* Callable: call it on a */
    PN r = potion_send(b, PN_call, a);
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
  if (la <= lb)
    for (i = la; i <= lb; i++) PN_PUSH(t, PN_NUM(i));
  else
    for (i = la; i >= lb; i--) PN_PUSH(t, PN_NUM(i));
  return t;
}

PN p6_reduce(Potion *P, PN cl, PN self, PN list, PN op) {
  /* [<=] reduce meta-op */
  P6_STUB("reduce meta-op");
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
  /* Z infix: zip two lists */
  P6_STUB("zip");
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

PN p6_atpos(Potion *P, PN cl, PN self, PN obj, PN idx) {
  /* @ indexing (positional) */
  if (PN_IS_TUPLE(obj)) {
    long i = PN_IS_NUM(idx) ? (long)PN_DBL(idx) : 0;
    if (i < 0 || i >= (long)PN_TUPLE_LEN(obj)) return PN_NIL;
    return PN_TUPLE_AT(obj, i);
  }
  return potion_send(obj, PN_STRN("at", 2), idx);
}

PN p6_atkey(Potion *P, PN cl, PN self, PN obj, PN key) {
  /* % indexing (associative) */
  if (PN_IS_TABLE(obj))
    return potion_table_at(P, PN_NIL, obj, key);
  return potion_send(obj, PN_STRN("at", 2), key);
}

/* ------------------------------------------------------------------ */
/* 16. Die / Redo */

PN p6_die(Potion *P, PN cl, PN self, PN msg) {
  if (PN_IS_STR(msg))
    potion_fatal(PN_STR_PTR(msg));
  else {
    PN s = potion_send(msg, PN_string);
    potion_fatal(PN_STR_PTR(s));
  }
  return PN_NIL;
}

PN p6_redo(Potion *P, PN cl, PN self) {
  P6_STUB("redo");
}

/* ---- test functions for roast6 (plan,ok,is,skip,todo) ---- */

static PN p6_test_counter;
static PN p6_test_plan;
PN p6_plan(Potion *P, PN cl, PN self, PN n) {
  if (PN_IS_NUM(n)) p6_test_plan = n;
  p6_test_counter = PN_NUM(0);
  printf("1..%ld\n", (long)PN_INT(n));
  fflush(stdout);
  return PN_NIL;
}

PN p6_ok(Potion *P, PN cl, PN self, PN cond, PN desc) {
  p6_test_counter = PN_NUM(PN_INT(p6_test_counter) + 1);
  if (PN_TEST(cond))
    printf("ok %ld - %s\n", (long)PN_INT(p6_test_counter),
           PN_IS_STR(desc) ? PN_STR_PTR(desc) : "");
  else
    printf("not ok %ld - %s\n", (long)PN_INT(p6_test_counter),
           PN_IS_STR(desc) ? PN_STR_PTR(desc) : "");
  fflush(stdout);
  return PN_NIL;
}

PN p6_test_is(Potion *P, PN cl, PN self, PN got, PN expected, PN desc) {
  p6_test_counter = PN_NUM(PN_INT(p6_test_counter) + 1);
  if (PN_INT(potion_send(got, PN_cmp, expected)) == 0)
    printf("ok %ld - %s\n", (long)PN_INT(p6_test_counter),
           PN_IS_STR(desc) ? PN_STR_PTR(desc) : "");
  else {
    printf("not ok %ld - %s\n", (long)PN_INT(p6_test_counter),
           PN_IS_STR(desc) ? PN_STR_PTR(desc) : "");
    if (PN_IS_STR(got))
      printf("#   got: %s\n", PN_STR_PTR(got));
    else
      printf("#   got: %ld\n", (long)PN_INT(got));
    if (PN_IS_STR(expected))
      printf("#   expected: %s\n", PN_STR_PTR(expected));
    else
      printf("#   expected: %ld\n", (long)PN_INT(expected));
  }
  return PN_NIL;
}

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

PN p6_regexp(Potion *P, PN cl, PN self, PN pat) {
  /* /pattern/ regex literal */
  P6_STUB("regexp");
}

PN p6_rx_p5(Potion *P, PN cl, PN self, PN pat) {
  /* Perl 5 regex embedded in p6 */
  P6_STUB("rx:p5");
}

/* ------------------------------------------------------------------ */
/* 20. Object system (OO) - stubs for now */

PN p6_role(Potion *P, PN cl, PN self, PN name, PN body) {
  P6_STUB("role");
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
  P6_STUB("try");
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

DLLEXPORT
void Potion_Init_libp6(Potion *P) {
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
  P6_METHOD(p6_atpos,   "a=o,b=o");
  P6_METHOD(p6_atkey,   "a=o,b=o");

  /* 12. Control flow */
  P6_METHOD(p6_die,     "a=o");
  P6_METHOD(p6_redo,    0);

  /* Test functions (plan,ok,is) */
  potion_method(P->lobby, "plan", p6_plan, "a=o");
  potion_method(P->lobby, "is",   p6_test_is,   "a=o,b=o,c=o");
  /* 13. Complex */
  P6_METHOD(p6_complex, "a=o,b=o");

  /* 14. Phasers */
  P6_METHOD(p6_begin,   "a=o,b=o");
  P6_METHOD(p6_end,     "a=o,b=o");

  /* 15. Regex */
  P6_METHOD(p6_regexp,  "a=o");
  P6_METHOD(p6_rx_p5,   "a=o");

  /* 16. OO - stubs */
  P6_METHOD(p6_role,       "a=o,b=o");
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
