/* p6test.c - the Test module of roast6 (plan ok is ...), included by libp6.c (one translation unit).
   TAP on stdout, diagnostics on stderr. */

static int p6t_count, p6t_planned, p6t_failed, p6t_ntodo;
static int p6t_sub_depth;  /* subtest nesting: each level indents by 4 spaces */
static char p6t_todo_reason[200];

static void p6t_indent(void) { for (int i = 0; i < p6t_sub_depth; i++) fputs("    ", stdout); }

/* Raku truthiness: undef, False, 0, "", "0", an empty list are false */
static int p6t_true(PN v) {
  if (!PN_TEST(v)) return 0;
  if (PN_IS_NUM(v)) return PN_DBL(v) != 0;
  if (PN_IS_STR(v)) return PN_STR_LEN(v) > 0 && !(PN_STR_LEN(v) == 1 && PN_STR_PTR(v)[0] == '0');
  if (PN_IS_TUPLE(v)) return PN_TUPLE_LEN(v) > 0;
  return 1;
}

static void p6t_report(int ok, PN desc) {
  char buf[256];
  const char *d = PN_IS_STR(desc) ? PN_STR_PTR(desc) : (desc == PN_NIL ? "" : p6_coerce_str(NULL, desc, buf, sizeof buf));
  p6t_count++;
  if (!ok && !p6t_ntodo) p6t_failed++;
  p6t_indent();
  printf("%sok %d", ok ? "" : "not ", p6t_count);
  if (d[0]) printf(" - %s", d);
  if (p6t_ntodo > 0) { printf(" # TODO %s", p6t_todo_reason); p6t_ntodo--; }
  printf("\n");
  fflush(stdout);
}

static const char *p6t_str(Potion *P, PN v, char *buf, size_t sz);
static const char *p6t_str(Potion *P, PN v, char *buf, size_t sz) {
  if (PN_IS_STR(v)) return PN_STR_PTR(v);
  if (PN_IS_TUPLE(v)) { /* Str of a List: the items joined by a space */
    PN r = PN_STR(""), sp = PN_STR(" ");
    PN_SIZE i;
    char b[64];
    for (i = 0; i < PN_TUPLE_LEN(v); i++) {
      PN e = PN_TUPLE_AT(v, i);
      PN es = PN_IS_STR(e) ? e : potion_str(P, p6t_str(P, e, b, sizeof b));
      if (i) r = potion_strcat(P, PN_STR_PTR(r), PN_STR_PTR(sp));
      r = potion_strcat(P, PN_STR_PTR(r), PN_STR_PTR(es));
    }
    return PN_STR_PTR(r);
  }
  return p6_coerce_str(P, v, buf, sz);
}

PN p6t_plan(Potion *P, PN cl, PN self, PN n) {
  if (PN_IS_NUM(n)) {
    /* PN_DBL handles both integer and double plans: @info/2 is a double, and
     * PN_INT on a PN_TDOUBLE reads the pointer bits (a garbage plan). */
    p6t_planned = (int)PN_DBL(n);
    p6t_indent();
    printf("1..%d\n", p6t_planned);
  } else if (PN_IS_STR(n)) { /* plan skip-all => 'reason' arrives as a string */
    p6t_indent();
    printf("1..0 # SKIP %s\n", PN_STR_PTR(n));
    fflush(stdout);
    exit(0);
  }
  fflush(stdout);
  return PN_NIL;
}
PN p6t_done_testing(Potion *P, PN cl, PN self, PN n) {
  if (!p6t_planned) printf("1..%d\n", p6t_count);
  fflush(stdout);
  return PN_NIL;
}
PN p6t_ok(Potion *P, PN cl, PN self, PN cond, PN desc) { int t = p6t_true(cond); p6t_report(t, desc); return t ? PN_TRUE : PN_FALSE; }
PN p6t_nok(Potion *P, PN cl, PN self, PN cond, PN desc) { int t = p6t_true(cond); p6t_report(!t, desc); return t ? PN_FALSE : PN_TRUE; }
PN p6t_pass(Potion *P, PN cl, PN self, PN desc) { p6t_report(1, desc); return PN_TRUE; }
PN p6t_flunk(Potion *P, PN cl, PN self, PN desc) { p6t_report(0, desc); return PN_FALSE; }

static void p6t_diag_got(Potion *P, const char *a, const char *b, int neg) {
  if (neg) fprintf(stderr, "#   got: '%s'\n#   expected: anything else\n", a);
  else fprintf(stderr, "#   expected: '%s'\n#        got: '%s'\n", b, a);
}
PN p6t_is(Potion *P, PN cl, PN self, PN got, PN expected, PN desc) {
  char ba[64], bb[64];
  const char *a = p6t_str(P, got, ba, sizeof ba), *b = p6t_str(P, expected, bb, sizeof bb);
  int ok = strcmp(a, b) == 0;
  p6t_report(ok, desc);
  if (!ok) p6t_diag_got(P, a, b, 0);
  return ok ? PN_TRUE : PN_FALSE;
}
PN p6t_isnt(Potion *P, PN cl, PN self, PN got, PN expected, PN desc) {
  char ba[64], bb[64];
  const char *a = p6t_str(P, got, ba, sizeof ba), *b = p6t_str(P, expected, bb, sizeof bb);
  int ok = strcmp(a, b) != 0;
  p6t_report(ok, desc);
  if (!ok) p6t_diag_got(P, a, b, 1);
  return ok ? PN_TRUE : PN_FALSE;
}
/* is-deeply / eqv: structural equality of lists, tables and scalars */
static int p6t_deep(Potion *P, PN a, PN b) {
  char ba[64], bb[64];
  PN_SIZE i;
  if (PN_IS_TUPLE(a) || PN_IS_TUPLE(b)) {
    if (!(PN_IS_TUPLE(a) && PN_IS_TUPLE(b)) || PN_TUPLE_LEN(a) != PN_TUPLE_LEN(b)) return 0;
    for (i = 0; i < PN_TUPLE_LEN(a); i++) if (!p6t_deep(P, PN_TUPLE_AT(a, i), PN_TUPLE_AT(b, i))) return 0;
    return 1;
  }
  return strcmp(p6t_str(P, a, ba, sizeof ba), p6t_str(P, b, bb, sizeof bb)) == 0;
}
PN p6t_is_deeply(Potion *P, PN cl, PN self, PN got, PN expected, PN desc) {
  int ok = p6t_deep(P, got, expected);
  p6t_report(ok, desc);
  return ok ? PN_TRUE : PN_FALSE;
}
PN p6t_like(Potion *P, PN cl, PN self, PN got, PN rx, PN desc) {
  int ok = p6_smartmatch(P, cl, self, got, rx) == PN_TRUE;
  p6t_report(ok, desc);
  return ok ? PN_TRUE : PN_FALSE;
}
PN p6t_unlike(Potion *P, PN cl, PN self, PN got, PN rx, PN desc) {
  int ok = p6_smartmatch(P, cl, self, got, rx) != PN_TRUE;
  p6t_report(ok, desc);
  return ok ? PN_TRUE : PN_FALSE;
}
PN p6t_cmp_ok(Potion *P, PN cl, PN self, PN got, PN op, PN expected, PN desc) {
  char ba[64], bb[64];
  const char *o = PN_IS_STR(op) ? PN_STR_PTR(op) : "==";
  const char *a = p6t_str(P, got, ba, sizeof ba), *b = p6t_str(P, expected, bb, sizeof bb);
  double da = PN_IS_NUM(got) ? PN_DBL(got) : atof(a), db = PN_IS_NUM(expected) ? PN_DBL(expected) : atof(b);
  int c = strcmp(a, b), ok;
  if (!strcmp(o, "==")) ok = da == db;
  else if (!strcmp(o, "!=")) ok = da != db;
  else if (!strcmp(o, "<")) ok = da < db;
  else if (!strcmp(o, "<=")) ok = da <= db;
  else if (!strcmp(o, ">")) ok = da > db;
  else if (!strcmp(o, ">=")) ok = da >= db;
  else if (!strcmp(o, "eq")) ok = c == 0;
  else if (!strcmp(o, "ne")) ok = c != 0;
  else if (!strcmp(o, "lt")) ok = c < 0;
  else if (!strcmp(o, "gt")) ok = c > 0;
  else if (!strcmp(o, "le")) ok = c <= 0;
  else if (!strcmp(o, "ge")) ok = c >= 0;
  else ok = 0;
  p6t_report(ok, desc);
  return ok ? PN_TRUE : PN_FALSE;
}
PN p6t_is_approx(Potion *P, PN cl, PN self, PN got, PN expected, PN desc) {
  double a = PN_IS_NUM(got) ? PN_DBL(got) : 0, b = PN_IS_NUM(expected) ? PN_DBL(expected) : 0, d = a - b;
  int ok = (d < 0 ? -d : d) < 1e-5 * (b < 0 ? -b : (b == 0 ? 1 : b));
  p6t_report(ok, desc);
  return ok ? PN_TRUE : PN_FALSE;
}
PN p6t_isa_ok(Potion *P, PN cl, PN self, PN obj, PN type, PN desc) {
  int ok = p6_smartmatch(P, cl, self, obj, type) == PN_TRUE;
  p6t_report(ok, PN_IS_STR(desc) ? desc : PN_STR("The object is-a type"));
  return ok ? PN_TRUE : PN_FALSE;
}
PN p6t_skip(Potion *P, PN cl, PN self, PN reason, PN n) {
  int i, k = PN_IS_NUM(n) ? (int)PN_INT(n) : 1;
  const char *r = PN_IS_STR(reason) ? PN_STR_PTR(reason) : "";
  for (i = 0; i < k; i++) { p6t_count++; printf("ok %d # SKIP %s\n", p6t_count, r); }
  fflush(stdout);
  return PN_NIL;
}
PN p6t_skip_rest(Potion *P, PN cl, PN self, PN reason) {
  const char *r = PN_IS_STR(reason) ? PN_STR_PTR(reason) : "";
  while (p6t_planned && p6t_count < p6t_planned) { p6t_count++; printf("ok %d # SKIP %s\n", p6t_count, r); }
  fflush(stdout);
  return PN_NIL;
}
PN p6t_todo(Potion *P, PN cl, PN self, PN reason, PN n) {
  snprintf(p6t_todo_reason, sizeof p6t_todo_reason, "%s", PN_IS_STR(reason) ? PN_STR_PTR(reason) : "");
  p6t_ntodo = PN_IS_NUM(n) ? (int)PN_INT(n) : 1;
  return PN_NIL;
}
PN p6t_diag(Potion *P, PN cl, PN self, PN msg) {
  char b[64];
  fprintf(stderr, "# %s\n", p6t_str(P, msg, b, sizeof b));
  return PN_NIL;
}
PN p6t_note(Potion *P, PN cl, PN self, PN msg) {
  char b[64];
  printf("# %s\n", p6t_str(P, msg, b, sizeof b));
  fflush(stdout);
  return PN_NIL;
}
/* lives-ok / dies-ok: the block runs under the p5 eval frame (libp5), p6 die calls the p5 die */
/* EVAL STRING: parse as p6, compile and run; a parse error dies */
static PN p6_eval_str(Potion *P, PN cl, PN self) {
  PN src = PN_CLOSURE(cl)->data[0], code;
  code = potion_syntax_parse(P, "p6", src, "<EVAL>");
  if (PN_TYPE(code) != PN_TSOURCE) return potion_send(P->lobby, potion_str(P, "die"), potion_str(P, "EVAL: parse error\n"));
  return p2_eval(P, code);
}
static PN p6_eval_closure(Potion *P, PN src) {
  PN c = potion_closure_new(P, (PN_F)p6_eval_str, potion_sig(P, 0), 1);
  PN_CLOSURE(c)->data[0] = src;
  return c;
}
PN p6_EVAL(Potion *P, PN cl, PN self, PN src, PN lang) {
  if (lang != PN_NIL) /* :lang<perl5> etc.: no other language runtimes */
    return potion_send(P->lobby, potion_str(P, "die"), potion_str(P, "EVAL: :lang is not supported\n"));
  if (!PN_IS_STR(src)) return src;
  return p6_eval_str(P, p6_eval_closure(P, src), self);
}
static int p6t_dies(Potion *P, PN block) {
  if (PN_IS_STR(block)) block = p6_eval_closure(P, block);
  PN r;
  if (!PN_IS_CLOSURE(block)) return 0;
  potion_define_global(P, potion_str(P, "$@"), potion_str(P, ""));
  r = potion_send(P->lobby, potion_str(P, "p5eval"), block);
  (void)r;
  { PN err = potion_send(P->lobby, potion_str(P, "$@"));
    return PN_IS_STR(err) && PN_STR_LEN(err) > 0; }
}
PN p6t_lives_ok(Potion *P, PN cl, PN self, PN block, PN desc) { int ok = !p6t_dies(P, block); p6t_report(ok, desc); return ok ? PN_TRUE : PN_FALSE; }
PN p6t_dies_ok(Potion *P, PN cl, PN self, PN block, PN desc) { int ok = p6t_dies(P, block); p6t_report(ok, desc); return ok ? PN_TRUE : PN_FALSE; }

/* subtest {..}, "desc": run the block as a nested TAP sub-test. The block's
 * plan/tests print indented (p6t_sub_depth); a single outer ok/not ok reflects
 * whether every sub-test passed. The block runs under the p5 eval frame, the
 * same way lives-ok/dies-ok run theirs. */
PN p6t_subtest(Potion *P, PN cl, PN self, PN block, PN desc) {
  { char dbuf[256];
    const char *d = desc ? p6t_str(P, desc, dbuf, sizeof dbuf) : "";
    fflush(stdout);
    p6t_indent();
    printf("# Subtest: %s\n", d ? d : "");
    fflush(stdout);
  }
  int outer_count = p6t_count, outer_planned = p6t_planned, outer_failed = p6t_failed;
  p6t_count = 0; p6t_planned = 0; p6t_failed = 0;
  p6t_sub_depth++;
  if (PN_IS_CLOSURE(block)) potion_send(P->lobby, potion_str(P, "p5eval"), block);
  int all_ok = (p6t_failed == 0);
  p6t_sub_depth--;
  p6t_count = outer_count; p6t_planned = outer_planned; p6t_failed = outer_failed;
  p6t_report(all_ok, desc);
  return all_ok ? PN_TRUE : PN_FALSE;
}

static void p6t_init(Potion *P) {
  potion_method(P->lobby, "plan", p6t_plan, "a=o");
  potion_method(P->lobby, "done-testing", p6t_done_testing, "|n=o");
  potion_method(P->lobby, "done_testing", p6t_done_testing, "|n=o");
  potion_method(P->lobby, "subtest", p6t_subtest, "block=o|desc=o");
  potion_method(P->lobby, "ok", p6t_ok, "cond=o|desc=o");
  potion_method(P->lobby, "nok", p6t_nok, "cond=o|desc=o");
  potion_method(P->lobby, "pass", p6t_pass, "|desc=o");
  potion_method(P->lobby, "flunk", p6t_flunk, "|desc=o");
  potion_method(P->lobby, "is", p6t_is, "got=o,expected=o|desc=o");
  potion_method(P->lobby, "isnt", p6t_isnt, "got=o,expected=o|desc=o");
  potion_method(P->lobby, "is-deeply", p6t_is_deeply, "got=o,expected=o|desc=o");
  potion_method(P->lobby, "is_deeply", p6t_is_deeply, "got=o,expected=o|desc=o");
  potion_method(P->lobby, "eqv", p6t_is_deeply, "got=o,expected=o|desc=o");
  potion_method(P->lobby, "like", p6t_like, "got=o,rx=o|desc=o");
  potion_method(P->lobby, "unlike", p6t_unlike, "got=o,rx=o|desc=o");
  potion_method(P->lobby, "cmp-ok", p6t_cmp_ok, "got=o,op=o,expected=o|desc=o");
  potion_method(P->lobby, "cmp_ok", p6t_cmp_ok, "got=o,op=o,expected=o|desc=o");
  potion_method(P->lobby, "is-approx", p6t_is_approx, "got=o,expected=o|desc=o");
  potion_method(P->lobby, "isa-ok", p6t_isa_ok, "obj=o,type=o|desc=o");
  potion_method(P->lobby, "isa_ok", p6t_isa_ok, "obj=o,type=o|desc=o");
  potion_method(P->lobby, "skip", p6t_skip, "|reason=o,n=o");
  potion_method(P->lobby, "skip-rest", p6t_skip_rest, "|reason=o");
  potion_method(P->lobby, "todo", p6t_todo, "reason=o|n=o");
  potion_method(P->lobby, "diag", p6t_diag, "msg=o");
  potion_method(P->lobby, "note", p6t_note, "msg=o");
  potion_method(P->lobby, "lives-ok", p6t_lives_ok, "block=o|desc=o");
  potion_method(P->lobby, "dies-ok", p6t_dies_ok, "block=o|desc=o");
  potion_method(P->lobby, "EVAL", p6_EVAL, "src=o|lang=o");
  potion_method(P->lobby, "eval_dies_ok", p6t_dies_ok, "block=o|desc=o");
  potion_method(P->lobby, "eval_lives_ok", p6t_lives_ok, "block=o|desc=o");
  potion_method(P->lobby, "lives_ok", p6t_lives_ok, "block=o|desc=o");
  potion_method(P->lobby, "dies_ok", p6t_dies_ok, "block=o|desc=o");
  potion_method(P->lobby, "skip_rest", p6t_skip_rest, "|reason=o");
  potion_method(P->lobby, "is_approx", p6t_is_approx, "got=o,expected=o|desc=o");
}
