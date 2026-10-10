# -*- mode: antlr; tab-width:8 -*-
#
# syntax-p5.y
# perl5 tokens and grammar
#
# (c) 2009 _why
# (c) 2013-2014 by perl11 org
#

%{
#ifndef P2
# define P2
#endif
#include "p2.h"
#include "internal.h"
#include "asm.h"
#include "ast.h"

#undef PN_AST
#undef PN_AST2
#undef PN_AST3
#undef PN_OP
#define PN_AST(T, A)        potion_source(P, AST_##T, A, PN_NIL, PN_NIL, G->lineno, P->line)
#define PN_AST2(T, A, B)    potion_source(P, AST_##T, A, B, PN_NIL, G->lineno, P->line)
#define PN_AST3(T, A, B, C) potion_source(P, AST_##T, A, B, C, G->lineno, P->line)
#define PN_OP(T, A, B)      potion_source(P, T, A, B, PN_NIL, G->lineno, P->line)

#define YYSTYPE PN
#define YY_XTYPE Potion *
#define YY_XVAR P

#define YY_INPUT(buf, result, max) { \
  YY_XTYPE P = G->data; \
  if (P->yypos < PN_STR_LEN(P->input)) { \
    result = max; \
    if (P->yypos + max > PN_STR_LEN(P->input)) \
      result = (PN_STR_LEN(P->input) - P->yypos); \
    PN_MEMCPY_N(buf, PN_STR_PTR(P->input) + P->yypos, char, result + 1); \
    P->yypos += max; \
  } else { \
    result = 0; \
  } \
}

#define YY_NAME(N) p5_code_##N

#define YY_TNUM 3
#define YY_TDEC 13

#ifdef YY_DEBUG
# define YYDEBUG_PARSE   DEBUG_PARSE
# define YYDEBUG_VERBOSE DEBUG_PARSE_VERBOSE

// -Dp: GC in the parser in potion_send fails in moved PNSource objects.
// we may still hold refs in the parser to old objects, G->ss not on the stack
# define YY_SET1(G, text, count, thunk, P) \
  yyprintf((stderr, "%s %d %p:<%s>\n", thunk->name, count,(void*)yy,\
           PN_STR_PTR(potion_send(yy, PN_string, 0)))); \
  G->val[count]= yy;
#endif

#define DEF_PSRC	(P->source?P->source:PN_TUP0())
//const char *Nullch = '\0';
#define SRC_TPL1(x)     P->source = PN_PUSH(DEF_PSRC, (x))
#define SRC_TPL2(x,y)   P->source = PN_PUSH(PN_PUSH(DEF_PSRC, (x)), (y))
#define SRC_TPL3(x,y,z) P->source = PN_PUSH(PN_PUSH(PN_PUSH(DEF_PSRC, (x)), (y)), (z))

static PN yylastline(struct _GREG *G, int pos);

typedef struct {
  long start;
  long end;
  long tag_start;
  long tag_len;
  long body_start;
  long body_len;
  int interpolate;
} P5Heredoc;

static long p5_line_end(const char *s, long len, long start, long *next) {
  long end = start;
  while (end < len && s[end] != '\n' && s[end] != '\r') end++;
  *next = end;
  if (*next < len && s[*next] == '\r') (*next)++;
  if (*next < len && s[*next] == '\n') (*next)++;
  return end;
}

/* Find a heredoc introducer on one source line. Quoted strings and comments
 * are skipped so eval strings containing heredocs are handled by the nested
 * p2_parse() call instead of by their outer parse. Requiring the delimiter to
 * immediately follow << also keeps ordinary spaced shift expressions such as
 * "WORD << 2" out of this lexical path. */
static int p5_find_heredoc(const char *input, long from, long end, int *quote_state,
                           P5Heredoc *h) {
  const char *s = input;
  long i = from;
  int quote = *quote_state;
  while (i < end) {
    unsigned char c = (unsigned char)s[i];
    if (quote) {
      if (c == '\\' && quote == '"' && i + 1 < end) i += 2;
      else {
        if (c == quote) quote = 0;
        i++;
      }
      continue;
    }
    if (c == '\'' || c == '"') {
      quote = c;
      i++;
      continue;
    }
    if (c == '#') {
      *quote_state = 0;
      return 0;
    }
    if (c != '<' || i + 2 >= end || s[i + 1] != '<') {
      i++;
      continue;
    }

    {
      long p = i + 2;
      int delimiter_quote = 0;
      h->interpolate = 1;
      if (s[p] == '\\') {
        h->interpolate = 0;
        p++;
      }
      if (p < end && (s[p] == '\'' || s[p] == '"')) {
        delimiter_quote = (unsigned char)s[p++];
        h->interpolate = delimiter_quote == '"';
      }
      h->tag_start = p;
      if (p >= end || !((s[p] >= 'A' && s[p] <= 'Z') ||
                        (s[p] >= 'a' && s[p] <= 'z') || s[p] == '_')) {
        i += 2;
        continue;
      }
      while (p < end && ((s[p] >= 'A' && s[p] <= 'Z') ||
                         (s[p] >= 'a' && s[p] <= 'z') ||
                         (s[p] >= '0' && s[p] <= '9') || s[p] == '_')) p++;
      h->tag_len = p - h->tag_start;
      if (delimiter_quote) {
        if (p >= end || s[p] != delimiter_quote) {
          i += 2;
          continue;
        }
        p++;
      }
      h->start = i;
      h->end = p;
      *quote_state = quote;
      return 1;
    }
  }
  *quote_state = quote;
  return 0;
}

static int p5_find_heredoc_body(const char *input, long len, long *cursor,
                                P5Heredoc *h) {
  const char *s = input;
  long pos = *cursor;
  h->body_start = pos;
  while (pos <= len) {
    long next, end = p5_line_end(s, len, pos, &next);
    if (end - pos == h->tag_len &&
        memcmp(s + pos, s + h->tag_start, (size_t)h->tag_len) == 0) {
      h->body_len = pos - h->body_start;
      *cursor = next;
      return 1;
    }
    if (next == pos) break;
    pos = next;
  }
  return 0;
}

static PNAsm *p5_write_heredoc(Potion *P, PNAsm * volatile out, const char *input,
                               const P5Heredoc *h) {
  long i;
  const char quote = h->interpolate ? '"' : '\'';
  out = potion_asm_write(P, out, (char *)&quote, 1);
  for (i = 0; i < h->body_len; i++) {
    char c = input[h->body_start + i];
    if (!h->interpolate && c == '\'')
      out = potion_asm_write(P, out, &c, 1);
    else if (h->interpolate && c == '"') {
      long j = i;
      while (j > 0 && input[h->body_start + j - 1] == '\\') j--;
      if ((i - j) % 2 == 0)
        out = potion_asm_write(P, out, "\\", 1);
    }
    out = potion_asm_write(P, out, &c, 1);
  }
  return potion_asm_write(P, out, (char *)&quote, 1);
}

/* Perl heredoc bodies occur after the complete introducer line, which a PEG
 * expression rule cannot consume in-place. Rewrite each introducer to the
 * equivalent existing single/double-quoted form before parsing, consuming
 * queued bodies in left-to-right order. This deliberately reuses str1/str2,
 * including their interpolation behavior, rather than adding a second string
 * AST builder. */
static PN p5_expand_heredocs(Potion *P, PN code) {
  long len = (long)PN_STR_LEN(code);
  /* GC may move or free the Perl string while the output asm buffer grows, so
   * scan a private copy of the source text. */
  char *input = malloc((size_t)len + 1);
  if (!input) return code;
  memcpy(input, PN_STR_PTR(code), (size_t)len);
  input[len] = 0;
  PNAsm * volatile out = NULL;
  long pos = 0;
  int quote_state = 0;

  while (pos < len) {
    long next, line_end = p5_line_end(input, len, pos, &next);
    long scan = pos, emit = pos, body_cursor = next;
    int on_line = 0;
    P5Heredoc h;

    while (p5_find_heredoc(input, scan, line_end, &quote_state, &h)) {
      if (!p5_find_heredoc_body(input, len, &body_cursor, &h)) {
        free(input);
        return code;
      }
      if (out)
        out = potion_asm_write(P, out, input + emit,
                               (size_t)(h.start - emit));
      else {
        out = potion_asm_new(P);
        out = potion_asm_write(P, out, input,
                               (size_t)h.start);
      }
      out = p5_write_heredoc(P, out, input, &h);
      emit = h.end;
      scan = h.end;
      on_line = 1;
    }
    if (out)
      out = potion_asm_write(P, out, input + emit,
                             (size_t)(next - emit));
    pos = on_line ? body_cursor : next;
  }
  free(input);
  if (!out) return code;
  out = potion_asm_write(P, out, "", 1);
  out->len--;
  return (PN)out;
}

/* split qw(...) capture text into a LIST of VALUE(string) AST nodes;
 * uses potion_source directly with an explicit lineno since the PN_AST
 * macro needs the complete GREG struct, unavailable in this prologue */
static PN p5_qw_words(Potion *P, long lineno, char *s, long len) {
  PN items = PN_TUP0();
  char *p = s, *e = s + len;
  while (p < e) {
    while (p < e && (*p==' '||*p=='\t'||*p=='\r'||*p=='\n'||*p=='\f'||*p=='\v')) p++;
    if (p >= e) break;
    char *w = p;
    while (p < e && !(*p==' '||*p=='\t'||*p=='\r'||*p=='\n'||*p=='\f'||*p=='\v')) p++;
    items = PN_PUSH(items, potion_source(P, AST_VALUE, PN_STRN(w, (long)(p - w)),
                                         PN_NIL, PN_NIL, lineno, PN_NIL));
  }
  return potion_source(P, AST_LIST, items, PN_NIL, PN_NIL, lineno, PN_NIL);
}

/* Start and finish a double-quote-like operator. The grammar rules for qq
 * share the same escape/interpolation actions as str2, but have several
 * possible delimiters. */
static void p5_dq_start(Potion *P) {
  P->pbuf = potion_asm_clear(P, P->pbuf);
  P->dqpieces = PN_TUP0();
}

static PN p5_dq_finish(Potion *P, long lineno) {
  PN last = potion_source(P, AST_VALUE,
                          potion_bytes_string(P, PN_NIL, (PN)P->pbuf),
                          PN_NIL, PN_NIL, lineno, P->line);
  if (PN_TUPLE_LEN(P->dqpieces) == 0)
    return last;
  {
    PN acc = PN_TUPLE_AT(P->dqpieces, 0);
    int i;
    for (i = 1; i < (int)PN_TUPLE_LEN(P->dqpieces); i++)
      acc = potion_source(P, AST_PLUS, acc, PN_TUPLE_AT(P->dqpieces, i),
                          PN_NIL, lineno, P->line);
    return potion_source(P, AST_PLUS, acc, last, PN_NIL, lineno, P->line);
  }
}

/* 'while (..) {..}' style statement. A helper (not an inline action) because
 * stmt has a variable named 'a', which the greg macros would expand inside
 * the '->a[]' member accesses. */
static PN p5_special_stmt(Potion *P, long lineno, PN m, PN l, PN b) {
  PN_SRC(m)->a[1] = PN_SRC(l);
  PN_SRC(m)->a[2] = PN_SRC(b);
  return potion_source(P, AST_EXPR, PN_TUP(m), PN_NIL, PN_NIL, lineno, P->line);
}

static PN p5_list_elem(Potion *P, long lineno, PN line, PN r, long i);
/* '($a, $b) = (LIST)': the whole rhs is evaluated into a temporary array
 * first, so '($x, $y) = ($y, $x)' swaps. A lone array rhs (@_, @a) keeps the
 * direct element access of p5_list_elem. */
static PN p5_aasign(Potion *P, long lineno, PN line, PN lhs, PN r) {
  static int ctr = 0;
  char nb[40];
  int nn = snprintf(nb, sizeof nb, "@__aa_%d", ctr++);
  PN items = PN_S(r, 0), s1 = PN_TUP0(), v, tmp;
  long i;
  if (!PN_IS_TUPLE(items) || PN_TUPLE_LEN(items) < 2) {
    PN_TUPLE_EACH(PN_S(lhs, 0), i, v, {
      s1 = PN_PUSH(s1, potion_source(P, AST_ASSIGN, v, p5_list_elem(P, lineno, line, r, i),
                                      PN_NIL, lineno, line));
    });
    return potion_source(P, AST_EXPR, s1, PN_NIL, PN_NIL, lineno, line);
  }
  tmp = potion_source(P, AST_MSG, PN_STRN(nb, nn), PN_NIL, PN_NIL, lineno, line);
  s1 = PN_PUSH(s1, potion_source(P, AST_ASSIGN, tmp, r, PN_NIL, lineno, line));
  PN_TUPLE_EACH(PN_S(lhs, 0), i, v, {
    PN el = potion_source(P, AST_MSG, PN_STRN(nb, nn),
              potion_source(P, AST_LIST,
                PN_TUP(potion_source(P, AST_VALUE, PN_NUM(i), PN_NIL, PN_NIL, lineno, line)),
                PN_NIL, PN_NIL, lineno, line), PN_NIL, lineno, line);
    s1 = PN_PUSH(s1, potion_source(P, AST_ASSIGN, v,
            potion_source(P, AST_EXPR, PN_TUP(el), PN_NIL, PN_NIL, lineno, line),
            PN_NIL, lineno, line));
  });
  return potion_source(P, AST_EXPR, s1, PN_NIL, PN_NIL, lineno, line);
}

static PN p5_flatten1(PN list_ast);
/* map/grep/sort BLOCK LIST: EXPR[LIST, MSG p5xxx(block)] -- the list is the
 * receiver (a lone array is used directly, not nested) */
static PN p5_listop(Potion *P, long lineno, PN line, const char *op, PN block, PN items) {
  /* a single item (@a, (1,2,3), keys %h) already is the list; the natives wrap a
   * lone scalar themselves */
  PN lst = (PN_IS_TUPLE(items) && PN_TUPLE_LEN(items) == 1)
    ? PN_TUPLE_AT(items, 0)
    : potion_source(P, AST_LIST, items, PN_NIL, PN_NIL, lineno, line);
  PN msg = block != PN_NIL
    ? potion_source(P, AST_MSG, PN_STR(op), PN_NIL, block, lineno, line)
    : potion_source(P, AST_MSG, PN_STR(op),
        potion_source(P, AST_LIST, PN_TUP(potion_source(P, AST_VALUE, PN_NIL, PN_NIL, PN_NIL, lineno, line)),
                      PN_NIL, PN_NIL, lineno, line), PN_NIL, lineno, line);
  return potion_source(P, AST_EXPR, PN_PUSH(PN_TUP(lst), msg), PN_NIL, PN_NIL, lineno, line);
}

/* 'local $x = EXPR;' inside a block: the thunks run in source order, so a
 * local statement records the variable name; the enclosing block (bracketed
 * by block-mark / the block action) then saves the old value in a temporary
 * at its start and restores it after the last statement. Limits: 'return',
 * 'die' and loop 'last'/'next' skip the restore; only plain scalars. */
#define P5_LOCAL_MAX 64
static PN p5_local_names[P5_LOCAL_MAX];
static int p5_local_n, p5_local_marks[16], p5_local_depth, p5_local_ctr;
static void p5_local_blockstart(void) {
  if (p5_local_depth < 16) p5_local_marks[p5_local_depth] = p5_local_n;
  p5_local_depth++;
}
static PN p5_local_note(PN name) {
  if (p5_local_n < P5_LOCAL_MAX) p5_local_names[p5_local_n++] = name;
  return name;
}
static PN p5_local_note_of(PN scalar_ast) { return p5_local_note(PN_S(scalar_ast, 0)); }
/* is STMT 'return EXPR'? -> the LIST ast of its argument, else NIL */
static PN p5_return_arg(PN stmt) {
  PN it;
  if (PN_PART(stmt) != AST_EXPR || !PN_IS_TUPLE(PN_S(stmt, 0)) ||
      PN_TUPLE_LEN(PN_S(stmt, 0)) != 1) return PN_NIL;
  it = PN_TUPLE_AT(PN_S(stmt, 0), 0);
  if (PN_PART(it) == AST_MSG && PN_S(it, 0) == PN_return && PN_S(it, 1) != PN_NIL &&
      PN_PART(PN_S(it, 1)) == AST_LIST && PN_IS_TUPLE(PN_S(PN_S(it, 1), 0)) &&
      PN_TUPLE_LEN(PN_S(PN_S(it, 1), 0)) == 1)
    return PN_TUPLE_AT(PN_S(PN_S(it, 1), 0), 0);
  return PN_NIL;
}
static PN p5_local_restores(Potion *P, long lineno, PN line, PN out, PN names, PN tmps, int n) {
  int i;
  for (i = 0; i < n; i++)
    out = PN_PUSH(out, potion_source(P, AST_ASSIGN,
            potion_source(P, AST_MSG, PN_TUPLE_AT(names, i), PN_NIL, PN_NIL, lineno, line),
            potion_source(P, AST_MSG, PN_TUPLE_AT(tmps, i), PN_NIL, PN_NIL, lineno, line),
            PN_NIL, lineno, line));
  return out;
}
static PN p5_local_blockend(Potion *P, long lineno, PN line, PN stmts) {
  int mark, i, nloc;
  long n, j;
  PN out, tmps = PN_TUP0(), names = PN_TUP0(), lv, lvm;
  char nb[40];
  int nn;
  p5_local_depth--;
  mark = (p5_local_depth >= 0 && p5_local_depth < 16) ? p5_local_marks[p5_local_depth] : p5_local_n;
  nloc = p5_local_n - mark;
  p5_local_n = mark;
  if (nloc <= 0 || !PN_IS_TUPLE(stmts)) return stmts;
  out = PN_TUP0();
  for (i = 0; i < nloc; i++) {
    PN name = p5_local_names[mark + i], tmp;
    nn = snprintf(nb, sizeof nb, "$__loc_%d", p5_local_ctr++);
    tmp = PN_STRN(nb, nn);
    names = PN_PUSH(names, name);
    tmps = PN_PUSH(tmps, tmp);
    out = PN_PUSH(out, potion_source(P, AST_ASSIGN,
            potion_source(P, AST_MSG, tmp, PN_NIL, PN_NIL, lineno, line),
            potion_source(P, AST_MSG, name, PN_NIL, PN_NIL, lineno, line),
            PN_NIL, lineno, line));
  }
  /* the block's value / 'return' value is computed BEFORE the restore */
  nn = snprintf(nb, sizeof nb, "$__lv_%d", p5_local_ctr++);
  lv = PN_STRN(nb, nn);
  lvm = potion_source(P, AST_MSG, lv, PN_NIL, PN_NIL, lineno, line);
  n = PN_TUPLE_LEN(stmts);
  for (j = 0; j < n; j++) {
    PN st = PN_TUPLE_AT(stmts, j), ra = p5_return_arg(st);
    if (ra != PN_NIL) {
      out = PN_PUSH(out, potion_source(P, AST_ASSIGN, lvm, ra, PN_NIL, lineno, line));
      out = p5_local_restores(P, lineno, line, out, names, tmps, nloc);
      out = PN_PUSH(out, potion_source(P, AST_EXPR, PN_TUP(
              potion_source(P, AST_MSG, PN_return,
                potion_source(P, AST_LIST, PN_TUP(lvm), PN_NIL, PN_NIL, lineno, line),
                PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line));
    } else if (j == n - 1 && PN_PART(st) == AST_EXPR) {
      out = PN_PUSH(out, potion_source(P, AST_ASSIGN, lvm, st, PN_NIL, lineno, line));
    } else {
      out = PN_PUSH(out, st);
    }
  }
  out = p5_local_restores(P, lineno, line, out, names, tmps, nloc);
  if (n > 0 && PN_PART(PN_TUPLE_AT(stmts, n - 1)) == AST_EXPR && p5_return_arg(PN_TUPLE_AT(stmts, n - 1)) == PN_NIL)
    out = PN_PUSH(out, potion_source(P, AST_EXPR, PN_TUP(lvm), PN_NIL, PN_NIL, lineno, line));
  return out;
}

/* Perl defines named subs at compile time, so a call may precede the 'sub'.
 * Top-level statements are reordered as
 *   1. 'name = undef' for every top-level assigned variable and sub name, so
 *      the names are locals of the main proto before any sub body is compiled
 *      (bodies then capture them by reference and see later assignments),
 *   2. all named sub definitions, in source order,
 *   3. the remaining statements.
 * Only BEGIN blocks (and use/require) run at parse time; everything else runs
 * in this order at run time. */
static PN p5_assign_name(PN st) {
  PN lhs, it;
  if (!PN_IS_PTR(st) || PN_PART(st) != AST_ASSIGN) return PN_NIL;
  lhs = PN_S(st, 0);
  if (!PN_IS_PTR(lhs)) return PN_NIL;
  if (PN_PART(lhs) == AST_EXPR && PN_IS_TUPLE(PN_S(lhs, 0)) && PN_TUPLE_LEN(PN_S(lhs, 0)) == 1)
    lhs = PN_TUPLE_AT(PN_S(lhs, 0), 0);
  if (!PN_IS_PTR(lhs) || PN_PART(lhs) != AST_MSG || PN_S(lhs, 1) != PN_NIL) return PN_NIL;
  it = PN_S(lhs, 0);
  return PN_IS_STR(it) ? it : PN_NIL;
}
static int p5_is_subdef(PN st) {
  PN rhs;
  if (!PN_IS_PTR(st) || PN_PART(st) != AST_ASSIGN || p5_assign_name(st) == PN_NIL) return 0;
  rhs = PN_S(st, 1);
  return PN_IS_PTR(rhs) && PN_PART(rhs) == AST_EXPR && PN_IS_TUPLE(PN_S(rhs, 0)) &&
         PN_TUPLE_LEN(PN_S(rhs, 0)) == 1 &&
         PN_PART(PN_TUPLE_AT(PN_S(rhs, 0), 0)) == AST_PROTO;
}
/* does the AST contain a message NAME (mention) / an assignment to NAME? */
static void p5_scan(PN n, PN name, int *mention, int *assigned) {
  long i;
  if (n == PN_NIL || !PN_IS_PTR(n)) return;
  if (PN_IS_TUPLE(n)) {
    for (i = 0; i < (long)PN_TUPLE_LEN(n); i++) p5_scan(PN_TUPLE_AT(n, i), name, mention, assigned);
    return;
  }
  if (potion_ptr_type(n) != PN_TSOURCE) return;
  if (PN_PART(n) == AST_MSG && PN_S(n, 0) == name) *mention = 1;
  if (PN_PART(n) == AST_ASSIGN && p5_assign_name(n) == name) *assigned = 1;
  for (i = 0; i < 3; i++) p5_scan(PN_S(n, i), name, mention, assigned);
}
/* is a sub called (mentioned without being assigned) by an EARLIER statement? */
static int p5_has_forward_ref(PN stmts) {
  long i, j;
  for (i = 0; i < (long)PN_TUPLE_LEN(stmts); i++) {
    PN st = PN_TUPLE_AT(stmts, i);
    if (!p5_is_subdef(st)) continue;
    for (j = 0; j < i; j++) {
      int m = 0, a = 0;
      p5_scan(PN_TUPLE_AT(stmts, j), p5_assign_name(st), &m, &a);
      if (m && !a) return 1;
    }
  }
  return 0;
}
static int p5_parse_depth;
static PN p5_predeclare(Potion *P, PN stmts) {
  PN pre = PN_TUP0(), subs = PN_TUP0(), rest = PN_TUP0(), seen = PN_TUP0();
  long i, j, k;
  /* not for a required file: its subs are spliced into a BEGIN block, and
   * hoisting there breaks compiled (.tc) programs (not root-caused) */
  if (p5_parse_depth > 1 || !PN_IS_TUPLE(stmts) || !p5_has_forward_ref(stmts)) return stmts;
  for (i = 0; i < (long)PN_TUPLE_LEN(stmts); i++) {
    PN st = PN_TUPLE_AT(stmts, i), names[64];
    int nn = 0;
    if (p5_is_subdef(st)) { subs = PN_PUSH(subs, st); }
    else rest = PN_PUSH(rest, st);
    if (PN_IS_PTR(st) && PN_PART(st) == AST_EXPR && PN_IS_TUPLE(PN_S(st, 0))) { /* my ($a,$b) = ... */
      for (k = 0; k < (long)PN_TUPLE_LEN(PN_S(st, 0)) && nn < 64; k++) {
        PN nm = p5_assign_name(PN_TUPLE_AT(PN_S(st, 0), k));
        if (nm != PN_NIL) names[nn++] = nm;
      }
    } else {
      PN nm = p5_assign_name(st);
      if (nm != PN_NIL) names[nn++] = nm;
    }
    for (k = 0; k < nn; k++) {
      for (j = 0; j < (long)PN_TUPLE_LEN(seen); j++)
        if (PN_TUPLE_AT(seen, j) == names[k]) break;
      if (j < (long)PN_TUPLE_LEN(seen)) continue;
      seen = PN_PUSH(seen, names[k]);
      pre = PN_PUSH(pre, potion_source(P, AST_ASSIGN,
              potion_source(P, AST_EXPR, PN_TUP(potion_source(P, AST_MSG, names[k], PN_NIL, PN_NIL, 1, P->line)), PN_NIL, PN_NIL, 1, P->line),
              potion_source(P, AST_VALUE, PN_NIL, PN_NIL, PN_NIL, 1, P->line), PN_NIL, 1, P->line));
    }
  }
  if (PN_TUPLE_LEN(subs) == 0) return stmts;       /* nothing to hoist */
  for (i = 0; i < (long)PN_TUPLE_LEN(subs); i++) pre = PN_PUSH(pre, PN_TUPLE_AT(subs, i));
  for (i = 0; i < (long)PN_TUPLE_LEN(rest); i++) pre = PN_PUSH(pre, PN_TUPLE_AT(rest, i));
  return pre;
}

/* Natives with optional parameters read garbage for omitted ones, so calls to
 * these builtins are padded with undef up to their full arity. */
static PN p5_pad_args(Potion *P, PN name, PN items) {
  static const struct { const char *n; int max; } pads[] = {
    {"substr", 3}, {"index", 3}, {"rindex", 3}, {"join", 7}, {"sprintf", 7}, {0, 0}
  };
  int i;
  PN nm = PN_S(name, 0);
  if (!PN_IS_STR(nm) || !PN_IS_TUPLE(items)) return items;
  for (i = 0; pads[i].n; i++)
    if (strlen(pads[i].n) == PN_STR_LEN(nm) && !memcmp(pads[i].n, PN_STR_PTR(nm), PN_STR_LEN(nm))) {
      while ((int)PN_TUPLE_LEN(items) < pads[i].max)
        items = PN_PUSH(items, potion_source(P, AST_VALUE, PN_NIL, PN_NIL, PN_NIL, 0, PN_NIL));
      break;
    }
  return items;
}

/* 'STMT while COND;' => while (COND) { STMT } (checked before each run, even
 * for do-blocks, unlike Perl). */
static PN p5_while_mod(Potion *P, long lineno, PN line, PN cond, PN stmt) {
  PN condlist = potion_source(P, AST_LIST, PN_TUP(cond), PN_NIL, PN_NIL, lineno, line);
  PN body = potion_source(P, AST_BLOCK, PN_TUP(stmt), PN_NIL, PN_NIL, lineno, line);
  PN whilemsg = potion_source(P, AST_MSG, PN_while, condlist, body, lineno, line);
  return potion_source(P, AST_EXPR, PN_TUP(whilemsg), PN_NIL, PN_NIL, lineno, line);
}

/* 'do BLOCK while COND;' => do BLOCK; while (COND) BLOCK  (body runs once first) */
static PN p5_do_while(Potion *P, long lineno, PN line, PN body, PN cond) {
  PN once = potion_source(P, AST_EXPR,
      PN_TUP(potion_source(P, AST_MSG, PN_STR("p5do"), PN_NIL, body, lineno, line)),
      PN_NIL, PN_NIL, lineno, line);
  PN condlist = potion_source(P, AST_LIST, PN_TUP(cond), PN_NIL, PN_NIL, lineno, line);
  PN whilemsg = potion_source(P, AST_MSG, PN_while, condlist, body, lineno, line);
  PN loop = potion_source(P, AST_EXPR, PN_TUP(whilemsg), PN_NIL, PN_NIL, lineno, line);
  return potion_source(P, AST_BLOCK, PN_PUSH(PN_TUP(once), loop), PN_NIL, PN_NIL, lineno, line);
}

/* C-style 'for (INIT; COND; STEP) BLOCK' => { INIT; while (COND) { BLOCK; STEP } }.
 * Known limit: 'next' jumps to the loop test and skips STEP. */
static PN p5_cfor(Potion *P, long lineno, PN line, PN init, PN cond, PN step, PN body) {
  PN stmts = PN_TUP0(), newstmts = PN_TUP0(), v;
  long i;
  if (cond == PN_NIL)
    cond = potion_source(P, AST_VALUE, PN_NUM(1), PN_NIL, PN_NIL, lineno, line);
  PN_TUPLE_EACH(PN_S(body, 0), i, v, { newstmts = PN_PUSH(newstmts, v); });
  if (step != PN_NIL) newstmts = PN_PUSH(newstmts, step);
  {
    PN newbody = potion_source(P, AST_BLOCK, newstmts, PN_NIL, PN_NIL, lineno, line);
    PN condlist = potion_source(P, AST_LIST, PN_TUP(cond), PN_NIL, PN_NIL, lineno, line);
    PN whilemsg = potion_source(P, AST_MSG, PN_while, condlist, newbody, lineno, line);
    if (init != PN_NIL) stmts = PN_PUSH(stmts, init);
    stmts = PN_PUSH(stmts, potion_source(P, AST_EXPR, PN_TUP(whilemsg), PN_NIL, PN_NIL, lineno, line));
  }
  return potion_source(P, AST_BLOCK, stmts, PN_NIL, PN_NIL, lineno, line);
}

/* a // b : a if it is defined, else b (a is evaluated twice) */
static PN p5_defor(Potion *P, long lineno, PN line, PN a, PN b) {
  PN nil = potion_source(P, AST_VALUE, PN_NIL, PN_NIL, PN_NIL, lineno, line);
  PN cond = potion_source(P, AST_NEQ, a, nil, PN_NIL, lineno, line);
  PN thn = potion_source(P, AST_BLOCK, PN_TUP(potion_source(P, AST_EXPR,
             PN_TUPIF(a), PN_NIL, PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line);
  PN els = potion_source(P, AST_BLOCK, PN_TUP(potion_source(P, AST_EXPR,
             PN_TUPIF(b), PN_NIL, PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line);
  PN mif = potion_source(P, AST_MSG, PN_if, cond, thn, lineno, line);
  PN mel = potion_source(P, AST_MSG, PN_else, PN_NIL, els, lineno, line);
  return potion_source(P, AST_EXPR, PN_PUSH(PN_TUP(mif), mel), PN_NIL, PN_NIL,
                       lineno, line);
}

PN p2_parse(Potion *, PN, char *);

/* 'require "file";' is expanded at parse time: the file is parsed and its
 * statements are spliced in as a block. (p2 has no run-time require yet, so
 * the file is found relative to the cwd, then in the -I directories.) */
static int p5_require_seen;
static PN p5_require(Potion *P, long lineno, PN line, const char *name, long len) {
  char buf[512], *path, *src;
  FILE *fp;
  long sz;
  PN code, stmts;
  if (len <= 0 || len >= (long)sizeof(buf)) return PN_TUP0();
  memcpy(buf, name, len);
  buf[len] = 0;
  path = potion_find_require(P, buf);
  if (!path) {
    fprintf(stderr, "** Can't locate %s\n", buf);
    return PN_TUP0();
  }
  fp = fopen(path, "rb");
  if (!fp) { free(path); return PN_TUP0(); }
  fseek(fp, 0, SEEK_END); sz = ftell(fp); fseek(fp, 0, SEEK_SET);
  src = malloc(sz + 1);
  if (!src || fread(src, 1, sz, fp) != (size_t)sz) {
    fclose(fp); free(src); free(path); return PN_TUP0();
  }
  fclose(fp);
  code = p2_parse(P, potion_str2(P, src, sz), path);
  free(src); free(path);
  stmts = (code && PN_IS_PTR(code) && potion_ptr_type(code) == PN_TSOURCE)
    ? PN_S(code, 0) : PN_NIL;
  p5_require_seen++;
  return potion_source(P, AST_BLOCK, stmts, PN_NIL, PN_NIL, lineno, line);
}

/* A BEGIN block that requires a file is not evaluated at parse time (the
 * definitions would be lost in the throwaway eval scope): it stays in the
 * program and runs in order. begin-mark / the BEGIN action bracket the block
 * and compare the number of requires seen (the thunks run in source order). */
static int p5_begin_marks[16], p5_begin_depth;
static void p5_begin_start(void) {
  if (p5_begin_depth < 16) p5_begin_marks[p5_begin_depth] = p5_require_seen;
  p5_begin_depth++;
}
static int p5_begin_end(void) {
  p5_begin_depth--;
  return p5_begin_depth >= 0 && p5_begin_depth < 16 &&
         p5_require_seen != p5_begin_marks[p5_begin_depth];
}

/* Perl subs see their arguments as @_. The VM has no varargs, so a plain
 * 'sub f {...}' whose body mentions @_ (shift, pop, $_[N] are rewritten to
 * @_ by the grammar) is compiled with P5_NARGS optional parameters
 * $__a0.. defaulting to the PN_NONE sentinel, and a prologue
 *   @_ = p5args($__a0, ..., $__aN);
 * which drops the unfilled tail (see potion_p5_args in core/table.c).
 * Calls with more than P5_NARGS arguments lose the rest. */
#define P5_NARGS 12

static int p5_uses_args(PN t) {
  int i;
  if (!t || !PN_IS_PTR(t)) return 0;
  if (PN_IS_TUPLE(t)) {
    for (i = 0; i < (int)PN_TUPLE_LEN(t); i++)
      if (p5_uses_args(PN_TUPLE_AT(t, i))) return 1;
    return 0;
  }
  if (potion_ptr_type(t) != PN_TSOURCE) return 0;
  if (PN_PART(t) == AST_PROTO) return 0; /* a nested sub has its own @_ */
  if (PN_PART(t) == AST_MSG && PN_IS_STR(PN_S(t, 0)) &&
      PN_STR_LEN(PN_S(t, 0)) == 2 && !memcmp(PN_STR_PTR(PN_S(t, 0)), "@_", 2))
    return 1;
  for (i = 0; i < 3; i++)
    if (p5_uses_args(PN_S(t, i))) return 1;
  return 0;
}

static PN p5_sub_proto(Potion *P, long lineno, PN line, PN body) {
  PN sig = PN_TUP0(), args = PN_TUP0(), stmts, call, assign;
  int i;
  char nm[16];
  if (!p5_uses_args(body))
    return potion_source(P, AST_PROTO, potion_source(P, AST_LIST, PN_NIL, PN_NIL,
                         PN_NIL, lineno, line), body, PN_NIL, lineno, line);
  /* The x86 JIT fills missing optional arguments at the call site from
   * protos[0]'s signature (not the callee's) and has too small an outgoing
   * argument area for P5_NARGS defaults: run programs with @_ subs in the
   * bytecode VM, which binds defaults from the callee's own signature. */
  if ((P->flags & ((1 << EXEC_BITS) - 1)) == EXEC_JIT)
    P->flags = (Potion_Flags)((P->flags & ~((1 << EXEC_BITS) - 1)) | EXEC_VM);
  for (i = 0; i < P5_NARGS; i++) {
    PN n, ref;
    snprintf(nm, sizeof(nm), "$__a%d", i);
    n = PN_STR(nm);
    sig = PN_PUSH(PN_PUSH(PN_PUSH(sig, n), PN_NUM(':')), PN_P5NOARG);
    ref = potion_source(P, AST_MSG, n, PN_NIL, PN_NIL, lineno, line);
    args = PN_PUSH(args, potion_source(P, AST_EXPR, PN_TUP(ref), PN_NIL, PN_NIL,
                                       lineno, line));
  }
  call = potion_source(P, AST_MSG, PN_STR("p5args"),
           potion_source(P, AST_LIST,
             PN_TUP(potion_source(P, AST_EXPR,
               PN_TUP(potion_source(P, AST_LIST, args, PN_NIL, PN_NIL, lineno, line)),
               PN_NIL, PN_NIL, lineno, line)),
             PN_NIL, PN_NIL, lineno, line),
           PN_NIL, lineno, line);
  assign = potion_source(P, AST_ASSIGN,
             potion_source(P, AST_MSG, PN_STR("@_"), PN_NIL, PN_NIL, lineno, line),
             potion_source(P, AST_EXPR, PN_TUP(call), PN_NIL, PN_NIL, lineno, line),
             PN_NIL, lineno, line);
  stmts = PN_PUSH(PN_TUP0(), assign);
  if (PN_S(body, 0) != PN_NIL) {
    PN old = PN_S(body, 0);
    for (i = 0; i < (int)PN_TUPLE_LEN(old); i++)
      stmts = PN_PUSH(stmts, PN_TUPLE_AT(old, i));
  }
  body = potion_source(P, AST_BLOCK, stmts, PN_NIL, PN_NIL, lineno, line);
  return potion_source(P, AST_PROTO,
           potion_source(P, AST_LIST, sig, PN_NIL, PN_NIL, lineno, line),
           body, PN_NIL, lineno, line);
}

/* RHS value for element i of 'my (...) = RHS'. A lone array on the right,
 * '= @_' or '= @a', is flattened to its i-th element; otherwise the i-th
 * list item is used. */
/* '(EXPR)' is always parsed as a one-element list literal. In scalar
 * contexts (rhs of a scalar assignment, ternary condition) it is plain
 * grouping: unwrap EXPR(LIST(EXPR)) to the inner expression. A list of
 * several items is left alone. */
static PN p5_unparen(PN e) {
  PN it;
  if (PN_PART(e) != AST_EXPR || !PN_IS_TUPLE(PN_S(e, 0)) ||
      PN_TUPLE_LEN(PN_S(e, 0)) != 1)
    return e;
  it = PN_TUPLE_AT(PN_S(e, 0), 0);
  if (PN_PART(it) != AST_LIST || !PN_IS_TUPLE(PN_S(it, 0)) ||
      PN_TUPLE_LEN(PN_S(it, 0)) != 1)
    return e;
  return PN_TUPLE_AT(PN_S(it, 0), 0);
}

/* $r->[i], $h->{k}, and chains like $r->[0]{k}[2]: chain is the tuple
 * [base, key1, key2, ...]. Reads send "at" for every step; an assignment
 * sends "put"(key, value) for the last one. Arrays are tuples and hashes
 * are tables, both heap objects, so they already behave as references. */
static PN p5_elem_op(Potion *P, long lineno, PN line, PN chain, const char *op,
                     PN value) {
  long i, n = PN_TUPLE_LEN(chain);
  PN out = PN_TUP(PN_TUPLE_AT(chain, 0));
  for (i = 1; i < n; i++) {
    int last = i == n - 1 && op != NULL;
    PN args, msg;
    PN key = PN_TUP(PN_TUPLE_AT(chain, i));
    if (last && value != PN_NIL) key = PN_PUSH(key, value);
    args = potion_source(P, AST_LIST, key, PN_NIL, PN_NIL, lineno, line);
    msg = potion_source(P, AST_MSG, PN_STR(last ? op : "at"), args, PN_NIL,
                        lineno, line);
    out = PN_PUSH(out, msg);
  }
  return potion_source(P, AST_EXPR, out, PN_NIL, PN_NIL, lineno, line);
}
static PN p5_elem_chain(Potion *P, long lineno, PN line, PN chain, PN value) {
  return p5_elem_op(P, lineno, line, chain, value != PN_NIL ? "put" : NULL, value);
}

/* push/unshift @a, i1, i2: EXPR[@a, push(i1), push(i2)]; every send returns
 * the array, so the chain works. unshift items go in reverse order. */
static PN p5_push(Potion *P, long lineno, PN line, PN op, PN arr, PN items) {
  long i, n = PN_TUPLE_LEN(items);
  PN out = PN_TUP(arr);
  for (i = 0; i < n; i++) {
    PN it = PN_TUPLE_AT(items, op != PN_NIL && PN_STR_PTR(op)[0] == 'u' ? n - 1 - i : i);
    PN args = potion_source(P, AST_LIST, PN_TUP(it), PN_NIL, PN_NIL, lineno, line);
    out = PN_PUSH(out, potion_source(P, AST_MSG, op, args, PN_NIL, lineno, line));
  }
  return potion_source(P, AST_EXPR, out, PN_NIL, PN_NIL, lineno, line);
}

/* {k => v, ...} anonymous hash: LIST -> table */
static PN p5_anonhash(Potion *P, long lineno, PN line, PN items) {
  PN lst = potion_source(P, AST_LIST, items, PN_NIL, PN_NIL, lineno, line);
  PN msg = potion_source(P, AST_MSG, PN_STR("table"), PN_NIL, PN_NIL, lineno, line);
  return potion_source(P, AST_EXPR, PN_PUSH(PN_TUP(lst), msg),
                       PN_NIL, PN_NIL, lineno, line);
}

/* '(1..5)' and '(@b)' on the rhs of an array assignment: the one item is
 * itself a list, so use it directly instead of nesting it in a one-element
 * tuple (see also the same special case in p5 foreach). */
static PN p5_flatten1(PN list_ast) {
  PN items = PN_S(list_ast, 0), only;
  if (!PN_IS_TUPLE(items) || PN_TUPLE_LEN(items) != 1) return list_ast;
  only = PN_TUPLE_AT(items, 0);
  if (PN_PART(only) == AST_EXPR && PN_IS_TUPLE(PN_S(only, 0)) &&
      PN_TUPLE_LEN(PN_S(only, 0)) == 1) {
    PN m = PN_TUPLE_AT(PN_S(only, 0), 0);
    if (PN_PART(m) == AST_MSG && PN_IS_STR(PN_S(m, 0))) {
      PN nm = PN_S(m, 0);
      if (PN_STR_LEN(nm) > 1 && PN_STR_PTR(nm)[0] == '@' && PN_S(m, 1) == PN_NIL)
        return only;
      if (PN_STR_LEN(nm) == 7 && !memcmp(PN_STR_PTR(nm), "p5range", 7))
        return only;
    }
  }
  return list_ast;
}

static PN p5_list_elem(Potion *P, long lineno, PN line, PN r, long i) {
  PN items = PN_S(r, 0);
  if (PN_TUPLE_LEN(items) == 1) {
    PN it = PN_TUPLE_AT(items, 0), m = PN_NIL;
    if (PN_PART(it) == AST_EXPR && PN_IS_TUPLE(PN_S(it, 0)) &&
        PN_TUPLE_LEN(PN_S(it, 0)) == 1)
      m = PN_TUPLE_AT(PN_S(it, 0), 0);
    if (m != PN_NIL && PN_PART(m) == AST_MSG && PN_S(m, 1) == PN_NIL &&
        PN_IS_STR(PN_S(m, 0)) && PN_STR_LEN(PN_S(m, 0)) > 1 &&
        PN_STR_PTR(PN_S(m, 0))[0] == '@') {
      PN idx = potion_source(P, AST_VALUE, PN_NUM(i), PN_NIL, PN_NIL, lineno, line);
      PN el = potion_source(P, AST_MSG, PN_S(m, 0),
                potion_source(P, AST_LIST, PN_TUP(idx), PN_NIL, PN_NIL, lineno, line),
                PN_NIL, lineno, line);
      return potion_source(P, AST_EXPR, PN_TUP(el), PN_NIL, PN_NIL, lineno, line);
    }
  }
  return potion_tuple_at(P, 0, items, PN_NUM(i));
}

/* Build a single-quote-like q value from the raw balanced capture. Only an
 * escaped delimiter or backslash loses its leading backslash, matching Perl's
 * non-interpolating quote rules. */
static PN p5_q_string(Potion *P, long lineno, char *s, long len,
                      unsigned char open, unsigned char close) {
  long i;
  P->pbuf = potion_asm_clear(P, P->pbuf);
  for (i = 0; i < len; i++) {
    if (s[i] == '\\' && i + 1 < len &&
        ((unsigned char)s[i + 1] == open ||
         (unsigned char)s[i + 1] == close || s[i + 1] == '\\')) {
      i++;
    }
    P->pbuf = potion_asm_write(P, P->pbuf, s + i, 1);
  }
  return potion_source(P, AST_VALUE,
                       potion_bytes_string(P, PN_NIL, (PN)P->pbuf),
                       PN_NIL, PN_NIL, lineno, P->line);
}

/* Encode Perl's compile-time /i, /m, /s, and /x modifiers as PCRE2 inline
 * options. Keeping the modifier with the pattern lets both =~ and the existing
 * String regex methods use the same two-argument runtime API. */
static PN p5_regexp(Potion *P, long lineno, char *literal, long len) {
  long close = len - 1;
  int caseless = 0, multiline = 0, dotall = 0, extended = 0;

  while (close > 0) {
    switch (literal[close]) {
    case 'i': caseless = 1; break;
    case 'm': multiline = 1; break;
    case 's': dotall = 1; break;
    case 'x': extended = 1; break;
    default: goto modifiers_done;
    }
    close--;
  }
modifiers_done:
  if (!caseless && !multiline && !dotall && !extended)
    return potion_source(P, AST_VALUE, PN_STRN(literal + 1, close - 1),
                         PN_NIL, PN_NIL, lineno, P->line);

  P->pbuf = potion_asm_clear(P, P->pbuf);
  P->pbuf = potion_asm_write(P, P->pbuf, "(?", 2);
  if (caseless) P->pbuf = potion_asm_write(P, P->pbuf, "i", 1);
  if (multiline) P->pbuf = potion_asm_write(P, P->pbuf, "m", 1);
  if (dotall) P->pbuf = potion_asm_write(P, P->pbuf, "s", 1);
  if (extended) P->pbuf = potion_asm_write(P, P->pbuf, "x", 1);
  P->pbuf = potion_asm_write(P, P->pbuf, ")", 1);
  P->pbuf = potion_asm_write(P, P->pbuf, literal + 1, close - 1);
  return potion_source(P, AST_VALUE,
                       potion_bytes_string(P, PN_NIL, (PN)P->pbuf),
                       PN_NIL, PN_NIL, lineno, P->line);
}

/* desugar 'for[each] [my] $x (LIST) { BODY }' into:
 *   my @__for_arr_N = LIST;
 *   my $__for_i_N = 0;
 *   while ($__for_i_N < @__for_arr_N->length) {
 *     $x = @__for_arr_N($__for_i_N);
 *     BODY
 *     $__for_i_N = $__for_i_N + 1;
 *   }
 * p2 has no native iterator protocol exposed to p5 yet, so this reuses
 * plain tuple indexing (call-with-index, same as $arr[i]) + the
 * existing Tuple#length method + the existing 'while' special-form
 * (MSG("while", cond_list, body)) instead of inventing new codegen. */
static PN p5_forlist(Potion *P, long lineno, PN line, PN loopvar, PN list_ast, PN body_block) {
  static int ctr = 0;
  int n = ctr++;
  char arrbuf[40], idxbuf[40];
  int an = snprintf(arrbuf, sizeof(arrbuf), "@__for_arr_%d", n);
  int in_ = snprintf(idxbuf, sizeof(idxbuf), "$__for_i_%d", n);
  PN arrname = PN_STRN(arrbuf, an);
  PN idxname = PN_STRN(idxbuf, in_);

  PN arrmsg  = potion_source(P, AST_MSG, arrname, PN_NIL, PN_NIL, lineno, line);
  PN idxmsg  = potion_source(P, AST_MSG, idxname, PN_NIL, PN_NIL, lineno, line);
  PN idxmsg2 = potion_source(P, AST_MSG, idxname, PN_NIL, PN_NIL, lineno, line);
  PN idxmsg3 = potion_source(P, AST_MSG, idxname, PN_NIL, PN_NIL, lineno, line);

  /* if the parenthesized iterable is exactly one bare array variable,
   * e.g. 'for my $x (@a)', use it directly as the assign RHS instead
   * of the list_ast (which would nest @a as a single list item instead
   * of flattening it -- same root cause as the documented '(EXPR) is
   * always a list-literal' gap). Bare 'for my $x (1,2,3)' or qw()
   * literals are unaffected: multi-item lists still use list_ast. */
  PN items0 = PN_S(list_ast, 0);
  PN for_rhs = list_ast;
  if (PN_TUPLE_LEN(items0) == 1) {
    PN only = potion_tuple_at(P, 0, items0, PN_NUM(0));
    if (PN_PART(only) == AST_EXPR && PN_TUPLE_LEN(PN_S(only,0)) == 1)
      only = potion_tuple_at(P, 0, PN_S(only,0), PN_NUM(0));
    if (PN_PART(only) == AST_MSG) {
      PN nm = PN_S(only, 0);
      if (PN_STR_LEN(nm) > 0 && PN_STR_PTR(nm)[0] == '@') for_rhs = only;
      else if (PN_STR_LEN(nm) == 7 && !memcmp(PN_STR_PTR(nm), "p5range", 7))
        for_rhs = potion_tuple_at(P, 0, items0, PN_NUM(0));
    } else if (PN_PART(only) == AST_LIST) {
      for_rhs = only;
    }
  }

  /* my @__for_arr_N = LIST; */
  PN stmt_arr = potion_source(P, AST_ASSIGN, arrmsg, for_rhs, PN_NIL, lineno, line);

  /* my $__for_i_N = 0; */
  PN zero = potion_source(P, AST_VALUE, PN_NUM(0), PN_NIL, PN_NIL, lineno, line);
  PN stmt_idx = potion_source(P, AST_ASSIGN, idxmsg, zero, PN_NIL, lineno, line);

  /* $x = @__for_arr_N($__for_i_N); */
  PN idxaccess = potion_source(P, AST_MSG, arrname,
                    potion_source(P, AST_LIST, PN_TUP(idxmsg2), PN_NIL, PN_NIL, lineno, line),
                    PN_NIL, lineno, line);
  PN stmt_bind = potion_source(P, AST_ASSIGN, loopvar, idxaccess, PN_NIL, lineno, line);

  /* $__for_i_N = $__for_i_N + 1; */
  PN one = potion_source(P, AST_VALUE, PN_NUM(1), PN_NIL, PN_NIL, lineno, line);
  PN incrval = potion_source(P, AST_PLUS, idxmsg3, one, PN_NIL, lineno, line);
  PN idxmsg4 = potion_source(P, AST_MSG, idxname, PN_NIL, PN_NIL, lineno, line);
  PN stmt_incr = potion_source(P, AST_ASSIGN, idxmsg4, incrval, PN_NIL, lineno, line);

  /* new body = [bind, ...orig body stmts..., incr] */
  PN newstmts = PN_TUP0();
  /* the index is advanced right after the bind, not at the end of the body:
   * 'next' jumps to the loop test and would otherwise skip the increment
   * and loop forever */
  newstmts = PN_PUSH(newstmts, stmt_bind);
  newstmts = PN_PUSH(newstmts, stmt_incr);
  { PN v; long i; PN origstmts = PN_S(body_block, 0);
    PN_TUPLE_EACH(origstmts, i, v, { newstmts = PN_PUSH(newstmts, v); }); }
  PN newbody = potion_source(P, AST_BLOCK, newstmts, PN_NIL, PN_NIL, lineno, line);

  /* $__for_i_N < @__for_arr_N->length */
  PN arrname2 = PN_STRN(arrbuf, an);
  PN arrmsg2 = potion_source(P, AST_MSG, arrname2, PN_NIL, PN_NIL, lineno, line);
  PN lenmsg = potion_source(P, AST_MSG, PN_STR("length"), PN_NIL, PN_NIL, lineno, line);
  PN lencall = potion_source(P, AST_EXPR, PN_PUSH(PN_TUP(arrmsg2), lenmsg), PN_NIL, PN_NIL, lineno, line);
  PN idxmsg5 = potion_source(P, AST_MSG, idxname, PN_NIL, PN_NIL, lineno, line);
  PN cond = potion_source(P, AST_LT, idxmsg5, lencall, PN_NIL, lineno, line);
  PN condlist = potion_source(P, AST_LIST, PN_TUP(cond), PN_NIL, PN_NIL, lineno, line);

  /* while (cond) newbody */
  PN whilemsg = potion_source(P, AST_MSG, PN_while, condlist, newbody, lineno, line);

  PN stmts = PN_TUP0();
  stmts = PN_PUSH(stmts, stmt_arr);
  stmts = PN_PUSH(stmts, stmt_idx);
  stmts = PN_PUSH(stmts, potion_source(P, AST_EXPR, PN_TUP(whilemsg), PN_NIL, PN_NIL, lineno, line));
  return potion_source(P, AST_BLOCK, stmts, PN_NIL, PN_NIL, lineno, line);
}

/* Perl's 'eq'/'ne' are STRING comparison operators, distinct from
 * '=='/'!=' (numeric) -- unlike Potion's native AST_EQ/AST_NEQ,
 * which compare by identity/bit-pattern (correct for '==' on two
 * numbers, or two interned strings, but NOT for comparing a number
 * to a string -- '1 == "1"'-shaped bit patterns never match). Wrap
 * each operand in a '->string' self-chained call (reusing every
 * type's existing #string method, e.g. potion_num_string) before
 * the existing AST_EQ/AST_NEQ, matching Perl's eq/ne coercion rule.
 * String literals are interned, so two different values that
 * stringify to the same text still compare equal afterwards. */
static PN p5_strval(Potion *P, long lineno, PN line, PN v) {
  PN m = potion_source(P, AST_MSG, PN_STR("string"), PN_NIL, PN_NIL, lineno, line);
  return potion_source(P, AST_EXPR, PN_PUSH(PN_TUP(v), m), PN_NIL, PN_NIL, lineno, line);
}
/* $s =~ s/PAT/REPL/flags  =>  $s = $s->subst("PAT", "REPL", global).
 * Only the i/m/s/x/g flags are handled; REPL is literal text using PCRE2's
 * $1/${1} group syntax (no Perl-variable interpolation, no /e). The
 * expression value is the new string, not Perl's substitution count. */
static PN p5_subst(Potion *P, long lineno, PN subject, PN pat, long patlen,
                   PN repl, int global, char *flags, long nflags) {
  char lit[patlen + nflags + 3];
  long n = 0, i;
  PN regex, valrepl, args, msg, call;
  lit[n++] = '/';
  memcpy(lit + n, PN_STR_PTR(pat), patlen); n += patlen;
  lit[n++] = '/';
  for (i = 0; i < nflags; i++)
    if (flags[i] != 'g') lit[n++] = flags[i];
  regex = p5_regexp(P, lineno, lit, n);
  valrepl = potion_source(P, AST_VALUE, repl, PN_NIL, PN_NIL, lineno, P->line);
  args = potion_source(P, AST_LIST,
      PN_PUSH(PN_PUSH(PN_TUP(regex), valrepl),
              potion_source(P, AST_VALUE, global ? PN_TRUE : PN_FALSE,
                            PN_NIL, PN_NIL, lineno, P->line)),
      PN_NIL, PN_NIL, lineno, P->line);
  msg = potion_source(P, AST_MSG, PN_STR("subst"), args, PN_NIL, lineno, P->line);
  call = potion_source(P, AST_EXPR, PN_PUSH(PN_TUP(subject), msg),
                       PN_NIL, PN_NIL, lineno, P->line);
  return potion_source(P, AST_ASSIGN, subject, call, PN_NIL, lineno, P->line);
}

static PN p5_matchval(Potion *P, long lineno, PN line, PN subject,
                      PN pattern, int negate) {
  PN args = potion_source(P, AST_LIST, PN_TUP(pattern), PN_NIL, PN_NIL,
                          lineno, line);
  PN msg = potion_source(P, AST_MSG, PN_STR("match"), args, PN_NIL,
                         lineno, line);
  PN call = potion_source(P, AST_EXPR, PN_PUSH(PN_TUP(subject), msg),
                          PN_NIL, PN_NIL, lineno, line);
  return negate
    ? potion_source(P, AST_NOT, call, PN_NIL, PN_NIL, lineno, line)
    : call;
}


%}

perl5 = -- s:statements end-of-file
   { $$ = P->source = PN_AST(CODE, p5_predeclare(P, s));
     s = (PN)(G->buf+G->pos);
     if (yyleng) YY_ERROR("** Syntax error");
     else if (*(char*)s) YY_ERROR("** Internal parser error: Couldn't parse all statements") }

# AST BLOCK captures lexicals
# Note that if/else blocks (mblock) do not capture lexicals
# block = '{' s:lineseq '}' { $$ = PN_AST(BLOCK, s) }

statements =
    s1:stmt           { $$ = s1 = PN_IS_TUPLE(s1) ? s1 : PN_TUP(s1) }
        (sep? s2:stmt { $$ = s1 = PN_PUSH(s1, s2) } )* sep?
    | ''              { $$ = PN_NIL }
begin-mark = '' { p5_begin_start(); }

stmt = pkgdecl
    | BEGIN begin-mark b:block  { if (p5_begin_end()) $$ = b;
                                  else { p2_eval(P, b); $$ = PN_TUP0(); } }
    | label s:stmt            { $$ = s }
    | "require" !utfw - ['"] < [^'"]* > ['"] - sep?
        { $$ = p5_require(P, G->lineno, P->line, yytext, yyleng) }
    | "require" !utfw - "q" "q"? '(' < [^)]* > ')' - sep?
        { $$ = p5_require(P, G->lineno, P->line, yytext, yyleng) }
    | "require" !utfw - modname - sep?  { $$ = PN_TUP0() }   # require Foo::Bar: not loaded
    | SUB n:id - semi -       { $$ = PN_TUP0() }   # forward declaration
    | subrout
    | USE "p6" - b:syntax-block --
        { $$ = PN_AST2(MSG, PN_p6, b) }
    | USE "v6" !utfw - b:syntax-block --
        { $$ = PN_AST2(MSG, PN_p6, b) }
    | u:use &(- (semi | !.)) sep?  { $$ = PN_TUP0() }
    # 'use Foo::Bar LIST;' / 'no Foo qw(..);': import lists are not evaluated yet
    | (USE|NO) modname - (!semi utf8)* sep?  { $$ = PN_TUP0() }
    | i:ifstmt                { $$ = PN_AST(EXPR, i) }
    | "do" !utfw - b:block WHILE e:ifnexpr sep?
      { $$ = p5_do_while(P, G->lineno, P->line, b, e) }
    | "do" !utfw - b:block UNTIL e:ifnexpr sep?
      { $$ = p5_do_while(P, G->lineno, P->line, b, PN_AST(NOT, PN_AST(EXPR, PN_TUPIF(e)))) }
    # our $x / @a / %h: a package variable is a plain lobby global here
    | OUR l:listvar &(- (semi | '}' | !.)) sep?
        { $$ = PN_AST2(ASSIGN, l, PN_AST(LIST, PN_NIL)) }
    | OUR s:stmt                 { $$ = s }
    | LOCAL l:scalar assign e:eqs - sep?
        { p5_local_note_of(l); $$ = PN_AST2(ASSIGN, l, p5_unparen(e)) }
    | LOCAL l:scalar - sep?
        { p5_local_note_of(l); $$ = PN_AST2(ASSIGN, l, PN_AST(VALUE, PN_NIL)) }
    | cforstmt
    | (FOR | FOREACH) l:list b:block     # for (LIST) {...}: the loop variable is $_
      { $$ = p5_forlist(P, G->lineno, P->line, PN_AST(MSG, PN_STR("$_")), l, b) }
    | forlist
    # 'while (...) {...}' is a complete statement: without this, a following
    # 'if (...)' on the next line was taken as its statement modifier.
    | m:special l:list b:block sep?
        { $$ = p5_special_stmt(P, G->lineno, m, l, b) }
    | a:returnstmt IF e:ifnexpr sep?
      { $$ = PN_OP(AST_AND, e, a) }
    | a:returnstmt UNLESS e:ifnexpr sep?
      { $$ = PN_OP(AST_AND, PN_AST(NOT, e), a) }
    | returnstmt sep?
    | a:assigndecl WHILE e:ifnexpr sep?
      { $$ = p5_while_mod(P, G->lineno, P->line, e, a) }
    | a:assigndecl UNTIL e:ifnexpr sep?
      { $$ = p5_while_mod(P, G->lineno, P->line, PN_AST(NOT, PN_AST(EXPR, PN_TUPIF(e))), a) }
    | a:assigndecl (FOR | FOREACH) l:formod-list sep?
      { $$ = p5_forlist(P, G->lineno, P->line, PN_AST(MSG, PN_STR("$_")), l,
                        PN_AST(BLOCK, PN_TUP(a))) }
    | a:assigndecl IF e:ifnexpr sep?
      { $$ = PN_OP(AST_AND, e, a) }
    | a:assigndecl UNLESS e:ifnexpr sep?
      { $$ = PN_OP(AST_AND, PN_AST(NOT, e), a) }
    | assigndecl sep?
    | block
    | a:sets WHILE e:ifnexpr sep?
      { $$ = p5_while_mod(P, G->lineno, P->line, e, a) }
    | a:sets UNTIL e:ifnexpr sep?
      { $$ = p5_while_mod(P, G->lineno, P->line, PN_AST(NOT, PN_AST(EXPR, PN_TUPIF(e))), a) }
    | a:sets (FOR | FOREACH) l:formod-list sep?
      { $$ = p5_forlist(P, G->lineno, P->line, PN_AST(MSG, PN_STR("$_")), l,
                        PN_AST(BLOCK, PN_TUP(a))) }
    | a:sets IF e:ifnexpr sep?
      { $$ = PN_OP(AST_AND, e, a) }
    | a:sets UNLESS e:ifnexpr sep?
      { $$ = PN_OP(AST_AND, PN_AST(NOT, e), a) }
    | s:sets
        ( or x:sets           { s = PN_OP(AST_OR, s, x) }
        | and x:sets          { s = PN_OP(AST_AND, s, x) })* sep?
                              { $$ = s }
    | s:sets sep?             { $$ = s }
    | l:list sep?             { $$ = PN_AST(EXPR, l) }

# Perl's hash-subscript/fat-comma auto-quote rule: a bareword
# identifier immediately followed by '=>' is a string literal, not a
# function call -- tried before the generic eqs/sets item so
# '(a=>1, b=>2)' stores string keys "a"/"b", matching how hashel's
# bareword-key lookup ($h{a}) already auto-quotes the same way.
fatkey = i:id &(- fatcomma) { $$ = PN_AST(VALUE, i) }

# 'my $x = EXPR' as an expression (e.g. '(my $x = f()) > 1')
myassign = MY i:global assign e:eqs -  { $$ = PN_AST2(ASSIGN, i, p5_unparen(e)) }
listitem = fatkey | myassign | range | sets
callitem = fatkey | range | sets
# a..b in list context: p5range(a, b) returns the tuple of integers
range = a:eqs - ".." !'.' - b:eqs
          { $$ = PN_AST(EXPR, PN_TUP(PN_AST2(MSG, PN_STR("p5range"),
                    PN_AST(LIST, PN_PUSH(PN_TUP(a), b))))) }

listexprs = e1:listitem      { $$ = e1 = PN_IS_TUPLE(e1) ? e1 : PN_TUP(e1) }
        ( - (comma|fatcomma) - e2:listitem   { $$ = e1 = PN_PUSH(e1, e2) } )*
        ( - comma )?   # trailing comma
# listexprs + named args: $x=1 (i.e. assignment)
callexprs = e1:callitem      { $$ = e1 = PN_IS_TUPLE(e1) ? e1 : PN_TUP(e1) }
        ( - (comma|fatcomma) - e2:callitem   { $$ = e1 = PN_PUSH(e1, e2) } )*
        ( - comma )?   # trailing comma

BEGIN   = "BEGIN" space+
PACKAGE = "package" space+
USE     = "use" space+
NO      = "no" space+
SUB     = "sub" space+
IF      = "if" space+
UNLESS  = "unless" space+
WHILE   = "while" space+
UNTIL   = "until" space+
ELSIF   = "elsif" space+
ELSE    = "else" space+
MY      = "my" space+
LOCAL   = "local" space+
OUR     = "our" space+
FOR     = "for" space+
FOREACH = "foreach" space+
RETURN  = "return" !utfw -

p5-siglist = list-start args2* list-end { $$ = PN_AST(LIST, P->source); P->source = PN_NIL }
#TODO: store name globally
# sub f ($$;@) {...}: a prototype is only sigils, so it is parsed and ignored
p5proto = '(' [$@%&*;\\ \t]* ')' -
subrout = SUB n:qid - p5proto b:block
          { $$ = PN_AST2(ASSIGN, PN_AST(EXPR, PN_TUP(PN_AST(MSG, n))),
                                 PN_AST(EXPR, PN_TUP(p5_sub_proto(P, G->lineno, P->line, b)))) }
        | SUB n:qid - l:p5-siglist b:block
          { $$ = PN_AST2(ASSIGN, PN_AST(EXPR, PN_TUP(PN_AST(MSG, n))),
                                 PN_AST(EXPR, PN_TUP(PN_AST2(PROTO, l, b)))) }
        | SUB n:qid - b:block
          { $$ = PN_AST2(ASSIGN, PN_AST(EXPR, PN_TUP(PN_AST(MSG, n))),
                                 PN_AST(EXPR, PN_TUP(p5_sub_proto(P, G->lineno, P->line, b)))) }
# no optional 'l:p5-siglist?' here: when the siglist is absent greg leaves
# the previous anonsub's stale 'l' in the slot (segfault in sig_compile).
anonsub = SUB p5proto b:block
        { $$ = p5_sub_proto(P, G->lineno, P->line, b) }
        | SUB l:p5-siglist b:block
        { $$ = PN_AST2(PROTO, l, b) }
        | SUB b:block
        { $$ = p5_sub_proto(P, G->lineno, P->line, b) }
# so far no difference in global or lex assignment
#subrout = SUB n:id - l:p5-siglist? a:subattrlist? b:block
#lexsubrout = MY - SUB n:subname p:proto? a:subattrlist? b:subbody
#        { $$ = PN_AST2(ASSIGN, n, PN_AST2(PROTO, p, b)) }
#subattrlist = ':' -? arg-name

# TODO: parse-time sideeffs: require + import, in the compiler its too late
use = (u:USE|u:NO) v:version
        { p2_eval(P, PN_AST(BLOCK, PN_TUP(PN_AST2(MSG, PN_use, PN_AST(LIST, PN_PUSH(PN_TUP(u), v)))))) }
    | u:USE n:id - "p2"          { P->flags |= MODE_P2 }
    | u:NO n:id - "p2"           { P->flags &= ~MODE_P2 }
    | (u:USE|u:NO) n:id
        { p2_eval(P, PN_AST(BLOCK, PN_TUP(PN_AST2(MSG, PN_use, PN_AST(LIST, PN_PUSH(PN_TUP(u), n)))))) }
    | (u:USE|u:NO) n:id fatcomma l:atom
        { p2_eval(P, PN_AST(BLOCK, PN_TUP(PN_AST2(MSG, PN_use, PN_AST(LIST, PN_PUSH(u,PN_PUSH(PN_PUSH(PN_TUP(u),n),l))))))) }

label = < [A-Z_] [A-Z0-9_]* > - ':' !':' -
# last/next map to potion's break/continue; a trailing LABEL is parsed but
# ignored (always the innermost loop).
loopctl = "last" !utfw - ([A-Z_][A-Z0-9_]* !utfw -)? { $$ = PN_AST(MSG, PN_break) }
        | "next" !utfw - ([A-Z_][A-Z0-9_]* !utfw -)? { $$ = PN_AST(MSG, PN_continue) }
modname = < utfw+ ('::' utfw+)* >
pkgname = < utfw+ ('::' utfw+)* > -  { $$ = PN_STRN(yytext, yyleng) }
pkgdecl = PACKAGE n:pkgname sep          { $$ = PN_TUP0() } # TODO: set namespace
        | PACKAGE n:pkgname v:version? b:block

# unless (COND) {...} [else {...}]: an if on the negated condition
ifstmt = UNLESS e:ifexpr s1:block ELSE s2:block
           { $$ = PN_PUSH(PN_TUP(PN_AST3(MSG, PN_if,
                    PN_AST(LIST, PN_TUP(PN_AST(NOT, e))), s1)),
                  PN_AST3(MSG, PN_else, PN_NIL, s2)) }
       | UNLESS e:ifexpr s:block         { $$ = PN_TUP(PN_OP(AST_AND, PN_AST(NOT, e), s)) }
       | IF e:ifexpr s:block !"els"   { $$ = PN_TUP(PN_OP(AST_AND, e, s)) }
       | IF e:ifexpr s1:block         { $$ = e = PN_AST3(MSG, PN_if, PN_AST(LIST, PN_TUP(e)), s1) }
         (ELSIF e1:ifexpr f:block     { $$ = e = PN_PUSH(PN_TUPIF(e), PN_AST3(MSG, PN_elsif, PN_AST(LIST, PN_TUP(e1)), f)) } )*
         (ELSE s2:block               { $$ = PN_PUSH(PN_TUPIF(e), PN_AST3(MSG, PN_else, PN_NIL, s2)) } )?
ifexpr = list-start eqs - list-end
ifnexpr = ifexpr | eqs

# the empty alternatives return NIL explicitly: an optional 'x:rule?' leaves
# the previous match's stale value in greg's slot when it does not match
# 'STMT for LIST;' list part: a parenthesized list, a range, or one expression
formod-list = l:list { $$ = l }
            | x:range { $$ = PN_AST(LIST, PN_TUP(x)) }
            | x:eqs { $$ = PN_AST(LIST, PN_TUP(x)) }
cfor-init = assigndecl | sets | '' { $$ = PN_NIL }
cfor-cond = eqs | '' { $$ = PN_NIL }
cfor-step = sets | '' { $$ = PN_NIL }
cforstmt = (FOR | FOREACH) list-start - i:cfor-init - semi - c:cfor-cond - semi - s:cfor-step - list-end b:block
            { $$ = p5_cfor(P, G->lineno, P->line, i, c, s, b) }
forlist = (FOR | FOREACH) i:lexglobal l:list b:block
            { $$ = p5_forlist(P, G->lineno, P->line, i, l, b) }

returnstmt = RETURN e:eqs -
               { PN m = PN_AST(MSG, PN_return);
                 PN_SRC(m)->a[1] = PN_SRC(PN_AST(LIST, PN_TUP(e)));
                 $$ = PN_AST(EXPR, PN_TUP(m)) }
           | RETURN -
               { $$ = PN_AST(EXPR, PN_TUP(PN_AST(MSG, PN_return))) }

assigndecl =
        # 'my @a;' / 'my %h;' start out as an empty array / hash, not undef
        MY l:listvar &(- (semi | '}' | !.))
          { $$ = PN_AST2(ASSIGN, l, PN_AST(LIST, PN_NIL)) }
      | MY l:hashvar &(- (semi | '}' | !.))
          { $$ = PN_AST2(ASSIGN, l, PN_AST(EXPR, PN_PUSH(PN_TUP(PN_AST(LIST, PN_NIL)),
                                                         PN_AST(MSG, PN_STR("table"))))) }
      |         MY t:name l:listvar assign r:list { PN_SRC(l)->a[2] = PN_SRC(t); $$ = PN_AST2(ASSIGN, l, r) }
      | MY? l:listvar assign r:list rep c:eqs   # my @a = (LIST) x N
          { $$ = PN_AST2(ASSIGN, l, PN_AST(EXPR, PN_PUSH(PN_TUP(r),
                   PN_AST2(MSG, PN_STR("repeat"), PN_AST(LIST, PN_TUP(c)))))) }
      | MY? l:listvar assign r:list       { $$ = PN_AST2(ASSIGN, l, p5_flatten1(r)) }
      | MY? l:hashvar assign r:list
          { PN m = PN_AST(MSG, PN_STR("table"));
            PN call = PN_AST(EXPR, PN_PUSH(PN_TUP(p5_flatten1(r)), m));
            $$ = PN_AST2(ASSIGN, l, call) }
      | MY t:name l:list assign r:list    # typed lists
          { PN s1 = PN_TUP0(); PN_TUPLE_EACH(PN_S(l,0), i, v, {
            PN_SRC(v)->a[2] = PN_SRC(t);
            s1 = PN_PUSH(s1, PN_AST2(ASSIGN, v, potion_tuple_at(P,0,PN_S(r,0),PN_NUM(i))));
          }); $$ = PN_AST(EXPR, s1) }
      | MY? l:list assign r:listrhs       # aasign
          { $$ = p5_aasign(P, G->lineno, P->line, l, r) }
      | c:elemchain assign e:eqs -  { $$ = p5_elem_chain(P, G->lineno, P->line, c, p5_unparen(e)) }
      | l:lexglobal assign e:sets -  { $$ = PN_AST2(ASSIGN, l, p5_unparen(e)) }  # sets: $a = $b = 0
      | l:global assign r:list      { YY_ERROR("** Assignment error") } # @x = () nyi

# right side of 'my (...) = ': a parenthesized list, or a lone array (@_, @a)
listrhs = list
        | v:listvar  { $$ = PN_AST(LIST, PN_TUP(PN_AST(EXPR, PN_TUP(v)))) }

#TODO most of these stack-like assign-expr cases can probably go away
sets = e:eqs
       ( assign s:sets       { e = PN_AST2(ASSIGN, e, s) }
       | or assign s:sets    { e = PN_AST2(ASSIGN, e, PN_OP(AST_OR, e, s)) }
       | and assign s:sets   { e = PN_AST2(ASSIGN, e, PN_OP(AST_AND, e, s)) }
       | pipe assign s:sets  { e = PN_AST2(ASSIGN, e, PN_OP(AST_PIPE, e, s)) }
       | caret assign s:sets { e = PN_AST2(ASSIGN, e, PN_OP(AST_CARET, e, s)) }
       | amp assign s:sets   { e = PN_AST2(ASSIGN, e, PN_OP(AST_AMP, e, s)) }
       | bitl assign s:sets  { e = PN_AST2(ASSIGN, e, PN_OP(AST_BITL, e, s)) }
       | bitr assign s:sets  { e = PN_AST2(ASSIGN, e, PN_OP(AST_BITR, e, s)) }
       | plus assign s:sets  { e = PN_AST2(ASSIGN, e, PN_OP(AST_PLUS, e, s)) }
       | minus assign s:sets { e = PN_AST2(ASSIGN, e, PN_OP(AST_MINUS, e, s)) }
       | times assign s:sets { e = PN_AST2(ASSIGN, e, PN_OP(AST_TIMES, e, s)) }
       | div assign s:sets   { e = PN_AST2(ASSIGN, e, PN_OP(AST_DIV, e, s)) }
       | rem assign s:sets   { e = PN_AST2(ASSIGN, e, PN_OP(AST_REM, e, s)) }
       | pow assign s:sets   { e = PN_AST2(ASSIGN, e, PN_OP(AST_POW, e, s)) }
       | dot assign s:sets   { e = PN_AST2(ASSIGN, e, PN_OP(AST_PLUS, e, s)) }
       | "//" assign s:sets  { e = PN_AST2(ASSIGN, e, p5_defor(P, G->lineno, P->line, e, s)) })?
       { $$ = e }

eqterm = c:cmps
      ( cmp x:cmps          { c = PN_OP(AST_CMP, c, x) }
      | numeq x:cmps        { c = PN_OP(AST_EQ, c, x) }
      | streq x:cmps        { c = PN_OP(AST_EQ, p5_strval(P, G->lineno, P->line, c),
                                              p5_strval(P, G->lineno, P->line, x)) }
      | numneq x:cmps       { c = PN_OP(AST_NEQ, c, x) }
      | strneq x:cmps       { c = PN_OP(AST_NEQ, p5_strval(P, G->lineno, P->line, c),
                                               p5_strval(P, G->lineno, P->line, x)) }
      | '=~' - 's' '/' p:sparg '/' r:sparg '/' f:sflags -
                            { c = p5_subst(P, G->lineno, c, p, PN_STR_LEN(p), r,
                                           memchr(PN_STR_PTR(f), 'g', PN_STR_LEN(f)) != NULL,
                                           PN_STR_PTR(f), PN_STR_LEN(f)) }
      | '=~' - x:regexp     { c = p5_matchval(P, G->lineno, P->line, c, x, 0) }
      | '=~' - x:scalar     { c = p5_matchval(P, G->lineno, P->line, c, x, 0) }
      | '!~' - x:scalar     { c = p5_matchval(P, G->lineno, P->line, c, x, 1) }
      | '!~' - x:regexp     { c = p5_matchval(P, G->lineno, P->line, c, x, 1) })*
      { $$ = c }

eqs = c:eqterm
      ( and !'=' x:eqterm      { c = PN_OP(AST_AND, c, x) }
      | or !'=' x:eqterm       { c = PN_OP(AST_OR, c, x) }
      | "xor" !utfw - x:eqterm { c = PN_OP(AST_AND, PN_OP(AST_OR, c, x),
                                         PN_AST(NOT, PN_AST(EXPR, PN_TUP(PN_OP(AST_AND, c, x))))) }
      | "//" !'=' - x:eqterm   { c = p5_defor(P, G->lineno, P->line, c, x) })*
      ( '?' - t:eqs - ':' - f:eqs -
        { c = p5_unparen(c); c = PN_AST(EXPR, PN_PUSH(PN_TUP(
                PN_AST3(MSG, PN_if, c,
                             PN_AST(BLOCK, PN_TUP(PN_AST(EXPR, PN_TUPIF(t)))))),
                PN_AST3(MSG, PN_else, PN_NIL,
                             PN_AST(BLOCK, PN_TUP(PN_AST(EXPR, PN_TUPIF(f))))))) }
      )?
      { $$ = c }


cmps = o:bitors
       ( gte x:bitors        { o = PN_OP(AST_GTE, o, x) }
       | gt x:bitors         { o = PN_OP(AST_GT, o, x) }
       | lte x:bitors        { o = PN_OP(AST_LTE, o, x) }
       | lt x:bitors         { o = PN_OP(AST_LT, o, x) })*
       { $$ = o }

bitors = a:bitand
         ( pipe x:bitand       { a = PN_OP(AST_PIPE, a, x) }
         | caret x:bitand      { a = PN_OP(AST_CARET, a, x) })*
         { $$ = a }

bitand = b:bitshift
         ( amp x:bitshift      { b = PN_OP(AST_AMP, b, x) })*
         { $$ = b }

bitshift = s:sum
           ( bitl x:sum          { s = PN_OP(AST_BITL, s, x) }
           | bitr x:sum          { s = PN_OP(AST_BITR, s, x) })*
           { $$ = s }

sum = p:product
      ( plus x:product      { p = PN_OP(AST_PLUS, p, x) }
      | minus x:product     { p = PN_OP(AST_MINUS, p, x) }
      | dot x:product       { p = PN_OP(AST_PLUS, p, x) })*
      { $$ = p }

product = p:power
          ( times x:power           { p = PN_OP(AST_TIMES, p, x) }
          | div x:power             { p = PN_OP(AST_DIV, p, x) }
          | rep x:power             { p = PN_AST(EXPR, PN_PUSH(PN_TUP(p),
                                        PN_AST2(MSG, PN_STR("repeat"), PN_AST(LIST, PN_TUP(x))))) }
          | rem x:power             { p = PN_OP(AST_REM, p, x) })*
          { $$ = p }

power = e:expr
        ( pow x:expr { e = PN_OP(AST_POW, e, x) })*
        { $$ = e }

# always a list
expr = o:fhop - "STDERR" !utfw - l:listexprs
        { PN acc = PN_TUPLE_AT(l, 0); long k;
          for (k = 1; k < (long)PN_TUPLE_LEN(l); k++) acc = PN_OP(AST_PLUS, acc, PN_TUPLE_AT(l, k));
          $$ = PN_AST(EXPR, PN_PUSH(PN_TUPIF(acc), PN_AST(MSG, o))) }
    | o:fhop - "STDOUT" !utfw - l:listexprs
        { $$ = PN_AST(EXPR, PN_PUSH(PN_TUPIF(PN_TUPLE_AT(l, 0)),
                                    PN_AST(MSG, PN_STR(PN_STR_LEN(o) == 8 ? "say" : "print")))) }
    | "scalar" !utfw - list-start - e:eqs - list-end -
        { $$ = PN_AST(EXPR, PN_PUSH(PN_TUPIF(e), PN_AST(MSG, PN_STR("length")))) }
    | "scalar" !utfw - e:eqs
        { $$ = PN_AST(EXPR, PN_PUSH(PN_TUPIF(e), PN_AST(MSG, PN_STR("length")))) }
    | "map" !utfw - b:block l:listexprs
        { $$ = p5_listop(P, G->lineno, P->line, "p5map", b, l) }
    | "grep" !utfw - b:block l:listexprs
        { $$ = p5_listop(P, G->lineno, P->line, "p5grep", b, l) }
    | "sort" !utfw - b:block l:listexprs
        { $$ = p5_listop(P, G->lineno, P->line, "p5sort", b, l) }
    | "sort" !utfw - list-start - l:listexprs - list-end -
        { $$ = p5_listop(P, G->lineno, P->line, "p5sort", PN_NIL, l) }
    | "sort" !utfw - l:listexprs
        { $$ = p5_listop(P, G->lineno, P->line, "p5sort", PN_NIL, l) }
    | "do" !utfw - b:block   { $$ = PN_AST(EXPR, PN_TUP(PN_AST3(MSG, PN_STR("p5do"), PN_NIL, b))) }
    | < ("say" | "print") > !utfw - &( (semi | '}' | FOR | FOREACH | IF | UNLESS | WHILE | UNTIL | !.))
        { $$ = PN_AST(EXPR, PN_PUSH(PN_TUP(PN_AST(MSG, PN_STR("$_"))),
                                    PN_AST(MSG, PN_STRN(yytext, yyleng)))) }  # bare say/print: $_
    | "eval" !utfw - b:block   { $$ = PN_AST(EXPR, PN_TUP(PN_AST3(MSG, PN_STR("p5eval"), PN_NIL, b))) }
    | c:elemchain      { $$ = p5_elem_chain(P, G->lineno, P->line, c, PN_NIL) }
    | c:p5exists       { $$ = PN_AST(EXPR, c) }
    | c:p5push         { $$ = c }
    | c:p5delete       { $$ = PN_AST(EXPR, c) }
    | c:p5coderef       { $$ = PN_AST(EXPR, c) }
    | c:loopctl         { $$ = PN_AST(EXPR, PN_TUP(c)) }
    | c:method  	        { $$ = PN_AST(EXPR, c) }
    | m:special l:list b:block  { PN_SRC(m)->a[1] = PN_SRC(l);
            PN_SRC(m)->a[2] = PN_SRC(b);
            $$ = PN_AST(EXPR, PN_TUP(m)) }
    | e:q                   { $$ = PN_AST(EXPR, PN_TUPIF(e)) }
    | e:qrexp               { $$ = PN_AST(EXPR, PN_TUPIF(e)) }
    # a bare /pattern/ matches $_
    | x:regexp              { $$ = p5_matchval(P, G->lineno, P->line,
                                  PN_AST(EXPR, PN_TUP(PN_AST(MSG, PN_STR("$_")))), x, 0) }
    | e:qq                  { $$ = PN_AST(EXPR, PN_TUPIF(e)) }
    | e:qw                  { $$ = PN_AST(EXPR, PN_TUPIF(e)) }
    # defined EXPR / defined(EXPR): a named unary operator, true unless undef
    | "defined" !utfw - list-start e:eqs list-end -
        { $$ = PN_OP(AST_NEQ, e, PN_AST(VALUE, PN_NIL)) }
    | "defined" !utfw - e:bitshift
        { $$ = PN_OP(AST_NEQ, e, PN_AST(VALUE, PN_NIL)) }
    | c:calllist		{ $$ = PN_AST(EXPR, c) }
    # named unary operator without parens binds tighter than comparison:
    # 'ord "A" == 65' is '(ord "A") == 65'
    | u:p5unary e:bitshift !(- (comma|fatcomma))
        { $$ = PN_AST(EXPR, PN_PUSH(PN_TUPIF(e), u)) }
    # bare 'shift' / 'pop' operate on @_
    | u:p5unary
        { $$ = PN_AST(EXPR, PN_PUSH(PN_TUP(PN_AST(MSG, PN_STR("@_"))), u)) }
    | c:call e:eqs !(- (comma|fatcomma)) 		{ $$ = PN_AST(EXPR, PN_PUSH(PN_TUPIF(e),
                                                            PN_TUPLE_AT(c,0))); }
    | c:call l:listexprs 	{ PN_SRC(PN_TUPLE_AT(c,0))->a[1] = PN_SRC(PN_AST(LIST, p5_pad_args(P, PN_TUPLE_AT(c,0), l)));
            $$ = PN_AST(EXPR, c); }
    | e:opexpr			{ $$ = e }
    | c:call			{ $$ = PN_AST(EXPR, c) }
    | e:eatom

eatom = e:atom                  { $$ = PN_AST(EXPR, PN_TUPIF(e)) }

opexpr = '\\' - e:expr		{ $$ = e }  # \@a, \%h, \&f: the object itself; \$x copies
    | not e:expr		{ $$ = PN_AST(NOT, e) }
    | bitnot e:expr		{ $$ = PN_AST(WAVY, e) }
    | minus  e:expr		{ $$ = PN_OP(AST_MINUS, PN_AST(VALUE, PN_ZERO), e) }
    | l:eatom times !times r:eatom { $$ = PN_OP(AST_TIMES, l, r) }
    | l:eatom div   !div r:eatom   { $$ = PN_OP(AST_DIV,  l, r) }
    | l:eatom minus !minus r:eatom { $$ = PN_OP(AST_MINUS, l, r) }
    | l:eatom plus !plus r:eatom   { $$ = PN_OP(AST_PLUS,  l, r) }
    | mminus e:mvalue		{ $$ = PN_OP(AST_INC, e, PN_NUM(-1) ^ 1) }
    | pplus e:mvalue		{ $$ = PN_OP(AST_INC, e, PN_NUM(1) ^ 1) }
    | e:mvalue (pplus		{ $$ = PN_OP(AST_INC, e, PN_NUM(1)) }
             | mminus		{ $$ = PN_OP(AST_INC, e, PN_NUM(-1)) }) {}

atom = e:anonhash | e:value | e:list | e:anonsub | e:qw

special = < ( "foreach"|"for"|"while"|"class"|"if"|"elseif" ) > - { $$ = PN_AST(MSG, PN_STRN(yytext, yyleng)) }

#FIXME methods and indirect methods:
#   chr 101  => (expr (value (101), msg ("chr")))
#   chr(101,1) => (expr (value (101), msg ("chr") list (value 1)))
#   print chr 101 => (expr (value (101), msg ("chr"), msg ("print")))
#   obj->meth(args) => (expr (msg obj), msg (meth) list (expr args))
#TODO: if (cond) {block} => expr (if, cond, block)
# callexprs allows assignment for named args
# p5 builtins that are 0-arg methods on their argument's type, not
# lobby functions: the paren form must self-chain like the bareword
# form (length $s == $s.length), otherwise calllist builds a bare
# PN_TUP(msg) sent to the lobby and silently returns undef. Whitelisted
# per name so user-defined subs called as foo(5) keep lobby semantics.
calllist = u:p5unary - list-start e:callitem - list-end -
           { $$ = PN_PUSH(PN_TUPIF(e), u) }
         | m:name - list-start - list-end
           { PN_SRC(m)->a[1] = PN_SRC(PN_AST(LIST, PN_NIL)); $$ = PN_TUP(m) }
         | m:name - l:list -
           { PN_SRC(m)->a[1] = PN_SRC(PN_AST(LIST, p5_pad_args(P, m, PN_S(l, 0)))); $$ = PN_TUP(m) }
         | m:name - list-start l:callexprs list-end -
           { PN_SRC(m)->a[1] = PN_SRC(PN_AST(LIST, p5_pad_args(P, m, l))); $$ = PN_TUP(m) }
call = m:name - { $$ = PN_TUP(m) }
# $r->[i] / $h->{k} / $r->[0]{k}: tuple [base, key1, key2, ...]
# the base is a plain scalar or an element: $r->[0], $a[0]->[1], $h{k}->{j}
elembase = b:scalar { $$ = b } | b:listel { $$ = b } | b:hashel { $$ = b }
elemchain = b:elembase k:elemstep1 { $$ = k = PN_PUSH(PN_TUP(b), k) }
            ( k2:elemstep { $$ = k = PN_PUSH(k, k2) } )*
elemstep1 = arrow k:elemkey { $$ = k }
elemstep = arrow? k:elemkey { $$ = k }
elemkey = '[' - i:eqs - ']' -  { $$ = i }
        | '{' - k:id - '}' -   { $$ = PN_AST(VALUE, k) }
        | '{' - k:eqs - '}' -  { $$ = k }
anonhash = '{' - s:listexprs - '}' -  { $$ = p5_anonhash(P, G->lineno, P->line, s) }
         | '{' - '}' -                { $$ = p5_anonhash(P, G->lineno, P->line, PN_NIL) }

# $cb->(args): call the closure in $cb. Same shape as a call of a local
# (MSG "$cb" with an arg LIST), which compile.c turns into
# getlocal/self/args/call.
p5coderef = s:scalar arrow l:list -
            { PN_SRC(s)->a[1] = PN_SRC(l); $$ = PN_TUP(s) }
# exists $h{k} / exists $h->{k}: table "exists" method
p5exists = "exists" !utfw - c:elemchain
           { $$ = PN_TUP(p5_elem_op(P, G->lineno, P->line, c, "exists", PN_NIL)) }
         | "exists" !utfw - '$' h:id - '{' - k:value - '}' -
           { $$ = PN_PUSH(PN_TUP(PN_AST(MSG, PN_STRCAT("%", PN_STR_PTR(h)))),
                          PN_AST2(MSG, PN_STR("exists"), PN_AST(LIST, PN_TUP(k)))) }
         | "exists" !utfw - '$' h:id - '{' - k:id - '}' -
           { $$ = PN_PUSH(PN_TUP(PN_AST(MSG, PN_STRCAT("%", PN_STR_PTR(h)))),
                          PN_AST2(MSG, PN_STR("exists"), PN_AST(LIST, PN_TUP(PN_AST(VALUE, k))))) }
# push/unshift @a, LIST: one push/unshift send per item, chained
pushop = < ("push" | "unshift") > !utfw - { $$ = PN_STRN(yytext, yyleng) }
p5push = o:pushop list-start - a:listvar - comma - l:listexprs - list-end -
           { $$ = p5_push(P, G->lineno, P->line, o, a, l) }
       | o:pushop a:listvar - comma - l:listexprs
           { $$ = p5_push(P, G->lineno, P->line, o, a, l) }
# delete $h{key}: send "delete" (removes key, returns the old value) to %h
p5delete = "delete" !utfw - c:elemchain
           { $$ = PN_TUP(p5_elem_op(P, G->lineno, P->line, c, "delete", PN_NIL)) }
         | "delete" !utfw - '$' h:id - '{' - k:value - '}' -
           { $$ = PN_PUSH(PN_TUP(PN_AST(MSG, PN_STRCAT("%", PN_STR_PTR(h)))),
                          PN_AST2(MSG, PN_STR("delete"), PN_AST(LIST, PN_TUP(k)))) }
         | "delete" !utfw - '$' h:id - '{' - k:id - '}' -
           { $$ = PN_PUSH(PN_TUP(PN_AST(MSG, PN_STRCAT("%", PN_STR_PTR(h)))),
                          PN_AST2(MSG, PN_STR("delete"), PN_AST(LIST, PN_TUP(PN_AST(VALUE, k))))) }
method = v:methlhs - arrow m:name - l:list -
         { PN_SRC(m)->a[1] = PN_SRC(l); $$ = PN_PUSH(PN_TUPIF(v), m) }
       | v:methlhs - arrow m:name -
         { $$ = PN_PUSH(PN_TUPIF(v), m) }

name = !keyword m:qid -      { $$ = PN_AST(MSG, m) }
     # &foo(...) / &foo: a call of the sub foo (subs are defined without the sigil)
     | !keyword '&' m:id -  { $$ = PN_AST(MSG, m) }
     | !keyword m:funcvar - { $$ = PN_AST(MSG, m) }

#listref-items = i1:listref-item     { $$ = i1 = PN_TUP(i1) }
#            (sep i2:listref-item { $$ = i1 = PN_PUSH(i1, i2) })*
#             sep?
#           | ''               { $$ = PN_NIL }
#
# TODO: unquoted lists
#listref-item = m:msg t:list v:loose { $$ = PN_AST3(LICK, m, v, t) }
#          | m:msg t:list { $$ = PN_AST3(LICK, m, PN_NIL, t) }
#          | m:msg v:loose t:list { $$ = PN_AST3(LICK, m, v, t) }
#          | m:msg v:loose { $$ = PN_AST2(LICK, m, v) }
#          | m:msg         { $$ = PN_AST(LICK, m) }

hash-item = k:value - (fatcomma|comma) v:atom { $$ = PN_AST2(ASSIGN, k, v) }
          | k:unquoted - fatcomma v:atom      { $$ = PN_AST2(ASSIGN, PN_AST(VALUE, k), v) }
hash-items = i1:hash-item      { $$ = i1 = PN_TUP(i1) }
            (sep i2:hash-item  { $$ = i1 = PN_PUSH(i1, i2) })*
             sep?
           | ''                { $$ = PN_NIL }

#loose = value
#      | v:unquoted { $$ = PN_AST(VALUE, v) }
#
# anonymous sub, w or w/o proto (aka list)
#sub = SUB n:arg-name - t:list? b:block       { $$ = PN_AST2(ASSIGN, n, PN_AST2(PROTO, t, b)) }
# the print/say verb of 'print STDERR ...' as the message to send: eprint / eprintln
fhop = "print" !utfw { $$ = PN_STR("eprint") } | "say" !utfw { $$ = PN_STR("eprintln") }
local-mark = '' { p5_local_blockstart(); }
block = block-start local-mark s:statements - block-end
        { $$ = PN_AST(BLOCK, p5_local_blockend(P, G->lineno, P->line, s)) }
# raw balanced-brace capture for use p6 { ... }; does not parse content.
# syntax-block-inner recurses without touching G->begin/G->end so the
# outer < > capture is not corrupted by inner braces.
syntax-block = '{' < syntax-block-inner > '}'
    { $$ = PN_STRN(yytext, yyleng) }
syntax-block-inner = (syntax-block-braced | !'}' .)*
syntax-block-braced = '{' syntax-block-inner '}'
list = list-start s:listexprs - list-end      { $$ = PN_AST(LIST, s) }
     | list-start list-end                    { $$ = PN_AST(LIST, PN_NIL) }
# [ ... ] is the list sent 'clone' (a fresh array): the extra EXPR keeps
# p5_unparen / p5_flatten1 from treating a one-element [x] as plain (x)
listref = listref-start s:listexprs - listref-end
          { $$ = PN_AST(EXPR, PN_PUSH(PN_TUP(PN_AST(LIST, s)), PN_AST(MSG, PN_STR("clone")))) }
     | listref-start listref-end
          { $$ = PN_AST(EXPR, PN_PUSH(PN_TUP(PN_AST(LIST, PN_NIL)), PN_AST(MSG, PN_STR("clone")))) }
hash = hash-start h:hash-items - hash-end     { $$ = PN_AST(LIST, h) }
     | hash-start hash-end                    { $$ = PN_AST(LIST, PN_NIL) }

#path = '/' < utfw+ > - { $$ = PN_STRN(yytext, yyleng) }
#path    = < utfw+ > -  { $$ = PN_STRN(yytext, yyleng) }
#msg = < utfw+ > -   	{ $$ = PN_STRN(yytext, yyleng) }

mvalue = i:immed - { $$ = PN_AST(VALUE, i) }
       | global

methlhs = global
        | name

value = i:immed - { $$ = PN_AST(VALUE, i) }
      | e:str2 -   { $$ = e }
      | e:qq -     { $$ = e }
      | global
      | listref
      | hash
immed = undef { $$ = PN_NIL }
#      | true  { $$ = PN_TRUE }
#      | false { $$ = PN_FALSE }
      | hex   { $$ = PN_NUM(PN_ATOI(yytext, yyleng, 16)) }
      | dec   { $$ = ($$ == YY_TDEC) ? potion_strtod(P, yytext, yyleng) : PN_NUM(PN_ATOI(yytext, yyleng, 10)) }
      | dec_wo_zero { potion_strtod(P, yytext, yyleng) }
      | str1

lexglobal = MY t:name i:global { PN_SRC(i)->a[2] = PN_SRC(t); $$ = i }
          | MY i:global        { $$ = i }
          | i:global

global  = scalar | listvar | hashvar | listel | hashel | funcvar | globvar
# special scalar vars
specialcaratscalar = < '^' [OCDFHIMPTVXNR] >
specialscalar = < '$' ( [@%!"$()0<>&`'+|/,.;?\\] | specialcaratscalar ) > # "
  { $$ = PN_STRN(yytext, yyleng) }  # without it 'i:specialscalar' read a stale slot
# send the value a msg, every global is a closure (see name)
scalar  = < '$' [1-9] [0-9]* > - !'[' !'{'     # $1: last match group
	  { $$ = PN_AST(MSG, PN_STRN(yytext, yyleng)) }
	| < '$' i:gid > - !'[' !'{'
	  { $$ = PN_AST(MSG, PN_STRCAT("$", PN_STR_PTR(i))) }
	| i:specialscalar - !'[' !'{'
	  { $$ = PN_AST(MSG, i) }
	# size of array
	| < '$' '#' l:id >  -
	  { $$ = PN_OP(AST_MINUS, 
                   PN_AST(EXPR, PN_PUSH(PN_TUP(PN_AST(MSG, PN_STRCAT("@", PN_STR_PTR(l)))),
	                                       PN_AST(MSG, PN_STR("length")))),
                   PN_AST(EXPR, PN_TUP(PN_AST(VALUE, PN_NUM(1))))) }
listvar = < '@' i:gid > - { $$ = PN_AST(MSG, PN_STRCAT("@", PN_STR_PTR(i))) }
        # @$r / @{$r}: arrays are tuples, so the reference already is the array
        | '@' '$' i:gid - { $$ = PN_AST(MSG, PN_STRCAT("$", PN_STR_PTR(i))) }
        | '@{' - '$' i:gid - '}' - { $$ = PN_AST(MSG, PN_STRCAT("$", PN_STR_PTR(i))) }
hashvar = < '%' i:gid > - { $$ = PN_AST(MSG, PN_STRCAT("%", PN_STR_PTR(i))) }
        | '%' '$' i:gid - { $$ = PN_AST(MSG, PN_STRCAT("$", PN_STR_PTR(i))) }
        | '%{' - '$' i:gid - '}' - { $$ = PN_AST(MSG, PN_STRCAT("$", PN_STR_PTR(i))) }
funcvar = < '&' i:id > - { $$ = PN_AST(MSG, PN_STRCAT("&", PN_STR_PTR(i))) }
globvar = < '*' i:id > - { $$ = PN_AST(MSG, PN_STRCAT("*", PN_STR_PTR(i))) }
listel  = < '$' l:gid - '[' - i:value - ']' > -
	  { $$ = PN_AST2(MSG, PN_STRCAT("@", PN_STR_PTR(l)),
	                      PN_AST(LIST, PN_TUP(i))) }
	# ?? used as $#[0] in base/lex.t
	| < '$' '#' - '[' - i:value - ']' >  -
	  { $$ = PN_AST2(MSG, PN_STR("@_"), PN_AST(LIST, PN_TUP(i))) }
hashel  = < '$' h:gid - '{' - k:value - '}' > -
          { $$ = PN_AST2(MSG, PN_STRCAT("%", PN_STR_PTR(h)),
                              PN_AST(LIST, PN_TUP(k))) }
        | < '$' h:gid - '{' - k:id - '}' > -
          { $$ = PN_AST2(MSG, PN_STRCAT("%", PN_STR_PTR(h)),
                              PN_AST(LIST, PN_TUP(PN_AST(VALUE, k)))) }

semi = ';'
comma = ','
fatcomma = "=>" -
arrow = "->" -
block-start = '{' -
block-end = semi? - '}' -
list-start = '(' -
list-end = ')' -
listref-start = '[' -
listref-end = ']' -
hash-start = '{' -
hash-end = '}' -
bitnot = '~' -
assign = '=' !'~' -
defassign = ":=" --
pplus = "++" -
mminus = "--" -
minus = '-' -
plus = '+' -
dot = '.' !'.' -
# repetition: 'x' as a word (not x=>, x=, or a longer identifier)
rep = 'x' !utfw !'=' !fatcomma -
times = '*' -
div = '/' -
rem = '%' -
pow = "**" -
bitl = "<<" -
bitr = ">>" -
amp = '&' -
caret = '^' -
pipe = '|' -
lt = '<' -
lte = "<=" -
gt = '>' -
gte = ">=" -
numneq = "!=" --
streq  = "eq" !utfw --
numeq  = "==" --
strneq = "ne" !utfw --
cmp = ("<=>" | "cmp" !utfw) --
p5unary = <( "length" | "ord" | "abs" | "chr" | "shift" | "pop" | "keys" | "values" | "ucfirst" | "lcfirst" | "lc" | "uc" | "reverse" | "ref" )> !utfw - { $$ = PN_AST(MSG, PN_STRN(yytext, yyleng)) }
and = ("&&" | "and" !utfw) --
or = ("||" | "or" !utfw) --
not = ("!" | "not" !utfw) --
# only compiler specific keywords
keyword = (("and" | "or" | "not" | "sub" | "return") !utfw)

undef = "undef" !utfw
#true = "true" !utfw
#false = "false" !utfw
hexl = [0-9A-Fa-f]
hex = '0x' < hexl+ >
# TODO allow _
dec = < '-'? ('0' | [1-9][0-9]* )
        ('.' [0-9]+ { $$ = YY_TDEC })?
        ('e' [-+] [0-9]+ { $$ = YY_TDEC })? >
dec_wo_zero = < '-'? '.' [0-9]+ >
version = 'v'? < ('0' | [1-9][0-9]*) ('.' [0-9]+ { $$ = YY_TDEC })? >
          { $$ = ($$ == YY_TDEC) ? PN_STRN(yytext, yyleng)
                                 : PN_NUM(PN_ATOI(yytext, yyleng, 10)) }

sparg = < ('\\' . | [^/\r\n])* > { $$ = PN_STRN(yytext, yyleng) }
sflags = < [gimsx]* > { $$ = PN_STRN(yytext, yyleng) }
# qr/PAT/flags: the (?flags)PAT string; usable as the rhs of =~
qrexp = "qr" !utfw - < '/' ('\\' . | [^/\r\n])* '/' [imsx]* > -
         { $$ = p5_regexp(P, G->lineno, yytext, yyleng); }
regexp = < '/' ('\\' . | [^/\r\n])* '/' [imsx]* > -
         { $$ = p5_regexp(P, G->lineno, yytext, yyleng); }

q1 = [']   # ' emacs highlight problems
c1 = < (!q1 utf8)+ > { P->pbuf = potion_asm_write(P, P->pbuf, yytext, yyleng) }
str1 = q1 { P->pbuf = potion_asm_clear(P, P->pbuf) }
       < (q1 q1 { P->pbuf = potion_asm_write(P, P->pbuf, "'", 1) } | c1)* >
       q1 { $$ = potion_bytes_string(P, PN_NIL, (PN)P->pbuf) }

esc         = '\\'
escn        = esc 'n' { P->pbuf = potion_asm_write(P, P->pbuf, "\n", 1) }
escb        = esc 'b' { P->pbuf = potion_asm_write(P, P->pbuf, "\b", 1) }
escf        = esc 'f' { P->pbuf = potion_asm_write(P, P->pbuf, "\f", 1) }
escr        = esc 'r' { P->pbuf = potion_asm_write(P, P->pbuf, "\r", 1) }
esct        = esc 't' { P->pbuf = potion_asm_write(P, P->pbuf, "\t", 1) }
escu        = esc 'u' < hexl hexl hexl hexl > {
  int nbuf = 0;
  char utfc[4] = {0, 0, 0, 0};
  unsigned long code = PN_ATOI(yytext, yyleng, 16);
  if (code < 0x80) {
    utfc[nbuf++] = code;
  } else if (code < 0x7ff) {
    utfc[nbuf++] = (code >> 6) | 0xc0;
    utfc[nbuf++] = (code & 0x3f) | 0x80;
  } else {
    utfc[nbuf++] = (code >> 12) | 0xe0;
    utfc[nbuf++] = ((code >> 6) & 0x3f) | 0x80;
    utfc[nbuf++] = (code & 0x3f) | 0x80;
  }
  P->pbuf = potion_asm_write(P, P->pbuf, utfc, nbuf);
}
escc = esc < utf8 > { P->pbuf = potion_asm_write(P, P->pbuf, yytext, yyleng) }

q2 = ["]
e2 = '\\' ["] { P->pbuf = potion_asm_write(P, P->pbuf, "\"", 1) }
c2 = < (!q2 !esc !(('$' (IDFIRST | [1-9] | '&' | '@' | '#' IDFIRST)) | ('@' IDFIRST)) utf8)+ > { P->pbuf = potion_asm_write(P, P->pbuf, yytext, yyleng) }
# "$a[1]" / "$a[$i]" / "$h{key}" / "$h{$k}": subscripted interpolation,
# same AST as the listel/hashel code rules but without their trailing
# whitespace skipping (which would eat literal spaces in the string).
dqel = '$' n:id '[' - i:mvalue - ']' {
  P->dqpieces = PN_PUSH(P->dqpieces, PN_AST(VALUE, potion_bytes_string(P, PN_NIL, (PN)P->pbuf)));
  P->dqpieces = PN_PUSH(P->dqpieces, PN_AST2(MSG, PN_STRCAT("@", PN_STR_PTR(n)),
                                                  PN_AST(LIST, PN_TUP(i))));
  P->pbuf = potion_asm_clear(P, P->pbuf);
}
     | '$' n:id '{' - k:id - '}' {
  P->dqpieces = PN_PUSH(P->dqpieces, PN_AST(VALUE, potion_bytes_string(P, PN_NIL, (PN)P->pbuf)));
  P->dqpieces = PN_PUSH(P->dqpieces, PN_AST2(MSG, PN_STRCAT("%", PN_STR_PTR(n)),
                                                  PN_AST(LIST, PN_TUP(PN_AST(VALUE, k)))));
  P->pbuf = potion_asm_clear(P, P->pbuf);
}
     | '$' n:id '{' - k:mvalue - '}' {
  P->dqpieces = PN_PUSH(P->dqpieces, PN_AST(VALUE, potion_bytes_string(P, PN_NIL, (PN)P->pbuf)));
  P->dqpieces = PN_PUSH(P->dqpieces, PN_AST2(MSG, PN_STRCAT("%", PN_STR_PTR(n)),
                                                  PN_AST(LIST, PN_TUP(k))));
  P->pbuf = potion_asm_clear(P, P->pbuf);
}
dqvar = dqel | dqmatch | dqlastidx | dqscalar | dqarray
# "$#a": last index; "@a": the elements joined with a space
dqlastidx = '$#' n:id {
  P->dqpieces = PN_PUSH(P->dqpieces, PN_AST(VALUE, potion_bytes_string(P, PN_NIL, (PN)P->pbuf)));
  P->dqpieces = PN_PUSH(P->dqpieces, PN_OP(AST_MINUS,
      PN_AST(EXPR, PN_PUSH(PN_TUP(PN_AST(MSG, PN_STRCAT("@", PN_STR_PTR(n)))), PN_AST(MSG, PN_STR("length")))),
      PN_AST(EXPR, PN_TUP(PN_AST(VALUE, PN_NUM(1))))));
  P->pbuf = potion_asm_clear(P, P->pbuf);
}
dqarray = '@' n:id {
  P->dqpieces = PN_PUSH(P->dqpieces, PN_AST(VALUE, potion_bytes_string(P, PN_NIL, (PN)P->pbuf)));
  P->dqpieces = PN_PUSH(P->dqpieces, PN_AST(EXPR, PN_PUSH(PN_TUP(PN_AST(MSG, PN_STRCAT("@", PN_STR_PTR(n)))),
      PN_AST2(MSG, PN_STR("join"), PN_AST(LIST, PN_TUP(PN_AST(VALUE, PN_STR(" "))))))));
  P->pbuf = potion_asm_clear(P, P->pbuf);
}
dqmatch = '$' < ( [1-9] [0-9]* | '&' | '@' ) > {
  PN nm = PN_STRN(yytext, yyleng);
  P->dqpieces = PN_PUSH(P->dqpieces, PN_AST(VALUE, potion_bytes_string(P, PN_NIL, (PN)P->pbuf)));
  P->dqpieces = PN_PUSH(P->dqpieces, PN_AST(MSG, PN_STRCAT("$", PN_STR_PTR(nm))));
  P->pbuf = potion_asm_clear(P, P->pbuf);
}
dqscalar = '$' < IDFIRST utfw* > {
  PN nm = PN_STRN(yytext, yyleng);
  P->dqpieces = PN_PUSH(P->dqpieces, PN_AST(VALUE, potion_bytes_string(P, PN_NIL, (PN)P->pbuf)));
  P->dqpieces = PN_PUSH(P->dqpieces, PN_AST(MSG, PN_STRCAT("$", PN_STR_PTR(nm))));
  P->pbuf = potion_asm_clear(P, P->pbuf);
}
str2 = q2 { P->pbuf = potion_asm_clear(P, P->pbuf); P->dqpieces = PN_TUP0(); }
       < (e2 | escn | escb | escf | escr | esct | escu | escc | dqvar | c2)* >
       q2 {
         PN last = PN_AST(VALUE, potion_bytes_string(P, PN_NIL, (PN)P->pbuf));
         if (PN_TUPLE_LEN(P->dqpieces) == 0) {
           $$ = last;
         } else {
           PN acc = PN_TUPLE_AT(P->dqpieces, 0);
           int pi;
           for (pi = 1; pi < (int)PN_TUPLE_LEN(P->dqpieces); pi++)
             acc = PN_OP(AST_PLUS, acc, PN_TUPLE_AT(P->dqpieces, pi));
           $$ = PN_OP(AST_PLUS, acc, last);
         }
       }

# q// and balanced paired-delimiter variants. The capture remains raw so
# p5_q_string can preserve non-delimiter backslashes and suppress interpolation.
q = "q" !utfw - (
      '/' < q-slash-item* > '/'
        { $$ = p5_q_string(P, G->lineno, yytext, yyleng, 0x2f, 0x2f); }
    | '(' < q-paren-item* > ')'
        { $$ = p5_q_string(P, G->lineno, yytext, yyleng, 0x28, 0x29); }
    | '[' < q-square-item* > ']'
        { $$ = p5_q_string(P, G->lineno, yytext, yyleng, 0x5b, 0x5d); }
    | '{' < q-brace-item* > '}'
        { $$ = p5_q_string(P, G->lineno, yytext, yyleng, 0x7b, 0x7d); }
    | q-angle-open < q-angle-item* > q-angle-close
        { $$ = p5_q_string(P, G->lineno, yytext, yyleng, 0x3c, 0x3e); }
    ) -

q-slash-item = esc utf8 | !'/' utf8
q-paren-item = esc utf8 | '(' q-paren-item* ')' | !')' utf8
q-square-item = esc utf8 | '[' q-square-item* ']' | !']' utf8
q-brace-item = esc utf8 | '{' q-brace-item* '}' | !'}' utf8
q-angle-open = '<'
q-angle-close = '>'
q-angle-item = esc utf8
             | q-angle-open q-angle-item* q-angle-close
             | !q-angle-close utf8

# qq// and paired-delimiter variants. Paired forms recurse so nested
# delimiters remain part of the value instead of terminating it early.
qq = "qq" !utfw - (
       '/' { p5_dq_start(P); }
         (escn | escb | escf | escr | esct | escu | escc | dqvar | qq-slash-c)*
       '/' { $$ = p5_dq_finish(P, G->lineno); }
     | '(' { p5_dq_start(P); }
         (escn | escb | escf | escr | esct | escu | escc | dqvar | qq-paren-nested | qq-paren-c)*
       ')' { $$ = p5_dq_finish(P, G->lineno); }
     | '[' { p5_dq_start(P); }
         (escn | escb | escf | escr | esct | escu | escc | dqvar | qq-square-nested | qq-square-c)*
       ']' { $$ = p5_dq_finish(P, G->lineno); }
     | '{' { p5_dq_start(P); }
         (escn | escb | escf | escr | esct | escu | escc | dqvar | qq-brace-nested | qq-brace-c)*
       '}' { $$ = p5_dq_finish(P, G->lineno); }
     ) -

qq-slash-c = < (!'/' !esc !('$' IDFIRST) utf8)+ >
             { P->pbuf = potion_asm_write(P, P->pbuf, yytext, yyleng); }

qq-paren-c = < (!'(' !')' !esc !('$' IDFIRST) utf8)+ >
             { P->pbuf = potion_asm_write(P, P->pbuf, yytext, yyleng); }
qq-paren-nested = '(' { P->pbuf = potion_asm_write(P, P->pbuf, "(", 1); }
                   (escn | escb | escf | escr | esct | escu | escc | dqvar | qq-paren-nested | qq-paren-c)*
                 ')' { P->pbuf = potion_asm_write(P, P->pbuf, ")", 1); }

qq-square-c = < (!'[' !']' !esc !('$' IDFIRST) utf8)+ >
              { P->pbuf = potion_asm_write(P, P->pbuf, yytext, yyleng); }
qq-square-nested = '[' { P->pbuf = potion_asm_write(P, P->pbuf, "[", 1); }
                    (escn | escb | escf | escr | esct | escu | escc | dqvar | qq-square-nested | qq-square-c)*
                  ']' { P->pbuf = potion_asm_write(P, P->pbuf, "]", 1); }

qq-brace-c = < (!'{' !'}' !esc !('$' IDFIRST) utf8)+ >
             { P->pbuf = potion_asm_write(P, P->pbuf, yytext, yyleng); }
qq-brace-nested = '{' { P->pbuf = potion_asm_write(P, P->pbuf, "\x7b", 1); }
                   (escn | escb | escf | escr | esct | escu | escc | dqvar | qq-brace-nested | qq-brace-c)*
                 '}' { P->pbuf = potion_asm_write(P, P->pbuf, "\x7d", 1); }


# qw(word list) literal, whitespace-separated words between any
# matching delimiter pair; !utfw keeps 'qwx(...)' a normal call.
# (No '<...>' form: '<' '>' collide with greg's capture syntax.)
qw = "qw" !utfw - '(' < [^)]* > ')' - { $$ = p5_qw_words(P, G->lineno, yytext, yyleng); }
   | "qw" !utfw - '[' < [^\]]* > ']' - { $$ = p5_qw_words(P, G->lineno, yytext, yyleng); }
   | "qw" !utfw - '{' < [^}]* > '}' - { $$ = p5_qw_words(P, G->lineno, yytext, yyleng); }
   | "qw" !utfw - '/' < [^/]* > '/' - { $$ = p5_qw_words(P, G->lineno, yytext, yyleng); }

unq-char = '{' unq-char+ '}'
         | '[' unq-char+ ']'
         | '(' unq-char+ ')'
         | !'#' !',' !'=>' !'{' !'[' !'(' !'}' !']' !')' utf8
unq-sep = sep !'#' !',' !'=>' !'{' !'[' !'('
unquoted = < (!unq-sep !listref-end unq-char)+ > { $$ = PN_STRN(yytext, yyleng) }

# lexer rules which are only printed with -DP, not with -Dp:
- = (pod | space | comment)*
-- = (pod | space | comment | semi)*
sep = semi (pod | space | comment | semi)*
# POD block: a line starting with =word up to a line starting with =cut
# (or EOF). The leading newline is part of the rule, so '=' in column 0 right
# after a newline only; a pod block in the very first line is not handled.
pod = end-of-line '=' [a-zA-Z]
      (!(end-of-line '=cut') (end-of-line | utf8))*
      (end-of-line '=cut' (!end-of-line utf8)*)?
comment	= '#' (!end-of-line utf8)*
# PSXSPC
# \240 U+A0 NO-BREAK SPACE
# \205 U+85 NEL
space = ' ' | '\f' | '\v' | '\t' | '\205' | '\240' | end-of-line
end-of-line = ( '\r\n' | '\n' | '\r' )
  { ++G->lineno; P->line = yylastline(G, thunk->begin); }
end-of-file = !'\0'
id = < IDFIRST utfw* > { $$ = PN_STRN(yytext, yyleng) }
# Foo::bar function name: package namespaces are not modelled, it is one flat name
qid = < IDFIRST utfw* ('::' IDFIRST utfw*)* > { $$ = PN_STRN(yytext, yyleng) }
# package-qualified variable name: $::x, $Foo::Bar::x
gid = < '::'? IDFIRST utfw* ('::' utfw+)* > { $$ = PN_STRN(yytext, yyleng) }
# isWORDCHAR && IDFIRST, no numbers
IDFIRST = [A-Za-z_]
     | '\304' [\250-\277]
     | [\305-\337] [\200-\277]
     | [\340-\357] [\200-\277] [\200-\277]
     | [\360-\364] [\200-\277] [\200-\277] [\200-\277]
# isWORDCHAR? \w and [:word:]
utfw = [A-Za-z0-9_]
     | '\304' [\252-\277]
     | [\305-\337] [\200-\277]
     | [\340-\357] [\200-\277] [\200-\277]
     | [\360-\364] [\200-\277] [\200-\277] [\200-\277]
# isWORDCHAR && XID_Continue
#IDCONT = [A-Za-z0-9_ ():\240-]
#     | '\304' [\250-\277]
#     | [\305-\337] [\200-\277]
#     | [\340-\357] [\200-\277] [\200-\277]
#     | [\360-\364] [\200-\277] [\200-\277] [\200-\277]
#IDPRINT = [\40-\176]
#     | [\302-\337] [\200-\277]
#     | [\340-\357] [\200-\277] [\200-\277]
#     | [\360-\364] [\200-\277] [\200-\277] [\200-\277]

utf8 = [\t\40-\176]
     | [\302-\337] [\200-\277]
     | [\340-\357] [\200-\277] [\200-\277]
     | [\360-\364] [\200-\277] [\200-\277] [\200-\277]
     | end-of-line

# for potion_sig, used in the runtime initialization
sig = args+ end-of-file
args = arg-list (arg-sep arg-list)*
arg-list = arg-set (optional arg-set)?
         | optional arg-set
arg-set = arg (comma - arg)*

arg-name = < utfw+ > - { $$ = PN_STRN(yytext, yyleng) }
arg-modifier = < ('-' | '\\' | '*' ) >  { $$ = PN_NUM(yytext[0]); }
# for FFIs, map to potion and C types. See potion_type_char()
arg-type = < [NBIDS&oTaubnsFPlkftxrcdm] > - { $$ = PN_NUM(yytext[0]) }
arg = m:arg-modifier n:arg-name assign t:arg-type
                        { SRC_TPL3(n,t,m) }
    | m:arg-modifier n:arg-name
                        { SRC_TPL3(n,PN_ZERO,m) }
    | n:arg-name assign t:arg-type
                        { SRC_TPL2(n,t) }
    | n:arg-name defassign d:value     # x:=0, optional
                        { SRC_TPL3(n, PN_NUM(':'), PN_S(d,0)) }
    # single types without name (N,o) as for FFIs forbidden, use (dummy=N) instead
    # | assign t:arg-type { SRC_TPL2(PN_STR(""),t) }
    | n:arg-name        { SRC_TPL1(n) }
optional = '|' -        { SRC_TPL1(PN_NUM('|')) }
arg-sep = '.' -         { SRC_TPL1(PN_NUM('.')) } #x,y... ignore rest

# p5 sigs. used by the seperate p2_sig, already in compiled 3-tuple format
sig_p5 = args2* end-of-file
args2 = arg2-list (arg2-yada)*
YADA = "..."
arg2-yada = YADA -  { SRC_TPL1(PN_NUM('.')) }
arg2-list = arg2-set (optional arg2-set)?
          | optional arg2-set
arg2-set = arg2 (comma - arg2)*

arg2-sigil = < [$@%] >          { $$ = PN_STRN(yytext, yyleng) }
arg2-name = s:arg2-sigil i:id - { $$ = potion_str_add(P, 0, s, i) }
# types are classes
arg2-type = !'$' i:id space+  { $$ = potion_class_find(P, i); if (!$$) yyerror(G,"Invalid signature type") }
arg2 = !arg2-sigil t:arg2-type m:arg-modifier n:arg2-name { SRC_TPL3(n,t,m) }
     | !arg2-sigil t:arg2-type n:arg2-name 		{ SRC_TPL2(n,t) }
     | m:arg-modifier n:arg2-name 		 	{ SRC_TPL3(n,PN_ZERO,m) }
     | n:arg2-name - assign d:value			{ SRC_TPL3(n,PN_NUM(':'), PN_S(d,0)) }
     | n:arg2-name					{ SRC_TPL1(n) }

%%

PN p2_parse(Potion *P, PN code, char *filename) {
  GREG *G = YY_NAME(parse_new)(P);
  int oldyypos = P->yypos;
  PN oldinput = P->input;
  PN oldsource = P->source;
  struct PNParseRoot root;
  code = p5_expand_heredocs(P, code);
  P->yypos = 0;
  P->input = code;
  P->source = PN_NIL;
  P->pbuf = potion_asm_new(P);
#ifdef DEBUG
  yydebug = P->flags;
#endif

  G->filename = filename;
  root.ss = &G->ss; root.vals = &G->vals; root.nvals = &G->valslen;
  root.prev = P->parse_roots;
  P->parse_roots = &root;
  P->fileno = PN_PUT(pn_filenames, PN_STR(filename));
  p5_parse_depth++;
  if (!YY_NAME(parse)(G)) {
    YY_ERROR("** Syntax error");
    fprintf(stderr, "%s", PN_STR_PTR(code));
  }
  p5_parse_depth--;
  P->parse_roots = root.prev;
  YY_NAME(parse_free)(G);

  code = P->source;
  P->source = oldsource;
  P->yypos = oldyypos;
  P->input = oldinput;
  return code;
}

// duplicate but still needed to compile internal methods
PN potion_sig(Potion *P, char *fmt) {
  PN out = PN_NIL;
  if (fmt == NULL) return PN_NIL;
  if (fmt[0] == '\0') return PN_FALSE;

  GREG *G = YY_NAME(parse_new)(P);
  int oldyypos = P->yypos;
  PN oldinput = P->input;
  PN oldsource = P->source;
  P->yypos = 0;
  P->input = potion_byte_str(P, fmt);
  P->source = out = PN_TUP0();
  P->pbuf = NULL;
#ifdef DEBUG
  yydebug = P->flags;
#endif

  if (!YY_NAME(parse_from)(G, yy_sig))
    YY_ERROR("** Signature syntax error");
  YY_NAME(parse_free)(G);

  out = P->source;
  P->source = oldsource;
  P->yypos = oldyypos;
  P->input = oldinput;
  return out;
}

PN p2_sig(Potion *P, char *fmt) {
  PN out = PN_NIL;
  if (fmt == NULL) return PN_NIL; // no signature, arg check off
  if (fmt[0] == '\0') return PN_FALSE; // empty signature, no args

  GREG *G = YY_NAME(parse_new)(P);
  int oldyypos = P->yypos;
  PN oldinput = P->input;
  PN oldsource = P->source;
  P->yypos = 0;
  P->input = potion_byte_str(P, fmt);
  P->source = out = PN_TUP0();
  P->pbuf = NULL;
#ifdef DEBUG
  yydebug = P->flags;
#endif

  if (!YY_NAME(parse_from)(G, yy_sig_p5))
    YY_ERROR("** Signature syntax error");
  YY_NAME(parse_free)(G);

  out = P->source;
  P->source = oldsource;
  P->yypos = oldyypos;
  P->input = oldinput;
  return out;
}

int potion_sig_find(Potion *P, PN cl, PN name)
{
  PN_SIZE idx = 0;
  PN sig;
  if (!PN_IS_CLOSURE(cl))
    cl = potion_obj_get_call(P, cl);

  if (!PN_IS_CLOSURE(cl))
    return -1;

  sig = PN_CLOSURE(cl)->sig;
  if (!PN_IS_TUPLE(sig))
    return -1;

  PN_TUPLE_EACH(sig, i, v, {
    if (v == PN_NUM(idx) || v == name)
      return idx;
    if (PN_IS_NUM(v)) idx++;
    else if (i < PN_TUPLE_LEN(sig) && PN_IS_STR(PN_TUPLE_AT(sig, i+1)))
      idx++;
  });

  return -1;
}

/** look back in the line for the prev. \n and back forth for the next \n
  */
static PN yylastline(struct _GREG *G, int pos) {
  char *c, *nl, *s = G->buf;
  int i, l;
  for (i=pos-1; i>=0 && (*(s+i) != 10); i--);
  if (i) nl = s+i+1; else nl = s;
  c = strchr(nl, 10);
  l = c ? c - nl : s + pos - nl;
  return l ? potion_byte_str2(G->data, nl, l) : PN_NIL;
}
