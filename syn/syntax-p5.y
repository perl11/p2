# -*- mode: antlr; tab-width:8 -*-
#
# syntax-p5.y
# perl5 tokens and grammar
#
# (c) 2009 _why
# (c) 2013-2014 by perl11.com
#

%{
#include <errno.h>
static int yy_uni(struct _GREG *G, int first);
static int p5_qgd_open(struct _GREG *G);
static int p5_globchars(const char *s);
static int p5_qgd_at(struct _GREG *G);
static int p5_mop_mark(struct _GREG *G);
static int p5_mop_step(struct _GREG *G, int kind);
static int p5_mop_done(struct _GREG *G, int kind);
#include <ctype.h>
#ifndef P2
# define P2
#endif
#include "p2.h"
#include "internal.h"
#include "khash.h"
#include "table.h"
/* a syntax error inside eval "string" is caught (sets $@, p5synfail in lib/p5/p5eval.c) */
static int p5_synfail(Potion *P, int line, const char *msg);
#define YY_ERROR(message) do { if (!p5_synfail(P, G->lineno, message)) yyerror(G, message); } while (0)
#include "asm.h"
#include "ast.h"

#undef PN_AST
#undef PN_AST2
#undef PN_AST3
#undef PN_OP
static PN p5_uni_warn_at(Potion *P, const char *file, long line, const char *name, int len);
#define p5_uni_warn(P, n, l) p5_uni_warn_at(P, G->filename, G->lineno, n, l)
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
    PN_MEMCPY_N(buf, PN_STR_PTR(P->input) + P->yypos, char, result); \
    if (result < max) ((char *)buf)[result] = 0; /* NUL only when it fits: yyrefill gives exactly max bytes */ \
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
      if (!delimiter_quote && p < end && s[p] == ';' && s[i + 2] == ';') { /* print <<;  the empty tag: the body ends at the first empty line */
        h->tag_len = 0;
        h->start = i;
        h->end = p;
        *quote_state = quote;
        return 1;
      }
      if (delimiter_quote) { /* <<"  --" / <<'END OF TEXT': everything up to the closing quote is the tag */
        while (p < end && s[p] != delimiter_quote) p++;
        if (p >= end || p == h->tag_start) {
          i += 2;
          continue;
        }
        h->tag_len = p - h->tag_start;
        p++;
      } else {
        if (p >= end || !((s[p] >= 'A' && s[p] <= 'Z') ||
                          (s[p] >= 'a' && s[p] <= 'z') || s[p] == '_')) {
          i += 2;
          continue;
        }
        while (p < end && ((s[p] >= 'A' && s[p] <= 'Z') ||
                           (s[p] >= 'a' && s[p] <= 'z') ||
                           (s[p] >= '0' && s[p] <= '9') || s[p] == '_')) p++;
        h->tag_len = p - h->tag_start;
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
    if (!h->interpolate && (c == '\'' || c == '\\'))
      out = potion_asm_write(P, out, "\\", 1);
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

static int p5_quasi_depth;
static int p5_lb_depth; /* inside a map/grep/sort block: `{ $_, 1 }` may end without a semicolon */
static int p5_lb(int d) { p5_lb_depth += d; return 1; }
static int p5_lb_fail(void) { p5_lb_depth--; return 0; }
static int p5_qd(int d) { if (d) { p5_quasi_depth += d; return 1; } return p5_quasi_depth == 0; } /* inside a quasi block a backtick is an unquote hole, not a command */
/* `cmd` / qx{cmd}: readpipe(STRING) */
static PN p5_readpipe_node(Potion *P, PN str) {
  return potion_source(P, AST_EXPR, PN_TUP(potion_source(P, AST_MSG, PN_STR("readpipe"),
           potion_source(P, AST_LIST, PN_TUP(str), PN_NIL, PN_NIL, 0, PN_NIL), PN_NIL, 0, PN_NIL)), PN_NIL, PN_NIL, 0, PN_NIL);
}
/* Start and finish a double-quote-like operator. The grammar rules for qq
 * share the same escape/interpolation actions as str2, but have several
 * possible delimiters. */
static void p5_dq_start(Potion *P) {
  P->pbuf = potion_asm_clear(P, P->pbuf);
  P->dqpieces = PN_TUP0();
}

static PN p5_cat2(Potion *P, long lineno, PN line, PN a, PN b);
static PN p5_listctx(Potion *P, PN item);
static PN p5_cond(Potion *P, long lineno, PN line, PN e);
static PN p5_block_cat(Potion *P, long lineno, PN line, PN b, PN c);
static PN p5_opmacro(Potion *P, long lineno, PN line, const char *op, PN a, PN b, PN fallback);
static PN p5_voidctx(Potion *P, PN stmt);
/* the pieces of a double quoted string are concatenated; \Q \U \L markers (a value "\1Q") wrap what
 * follows up to the \E (or the end) in quotemeta, uc or lc */
/* append the code point CODE as UTF-8 to the string literal being scanned */
static void p5_put_cp(Potion *P, unsigned long code) {
  char u[8];
  int n = 0;
  if (code < 0x80) u[n++] = (char)code;
  else if (code < 0x800) { u[n++] = (code >> 6) | 0xc0; u[n++] = (code & 0x3f) | 0x80; }
  else if (code < 0x10000) { u[n++] = (code >> 12) | 0xe0; u[n++] = ((code >> 6) & 0x3f) | 0x80; u[n++] = (code & 0x3f) | 0x80; }
  else if (code < 0x200000) { u[n++] = (code >> 18) | 0xf0; u[n++] = ((code >> 12) & 0x3f) | 0x80; u[n++] = ((code >> 6) & 0x3f) | 0x80; u[n++] = (code & 0x3f) | 0x80; }
  else { /* perl's extended UTF-8: 5 and 6 byte forms up to 0x7FFFFFFF, 0xFE + 6 bytes beyond */
    int len = code < 0x4000000 ? 5 : code < 0x80000000UL ? 6 : 7, k;
    u[0] = len == 5 ? 0xf8 : len == 6 ? 0xfc : 0xfe;
    if (len < 7) u[0] |= code >> (6 * (len - 1));
    for (k = 1; k < len; k++) u[k] = ((code >> (6 * (len - 1 - k))) & 0x3f) | 0x80;
    n = len;
  }
  P->pbuf = potion_asm_write(P, P->pbuf, u, n);
}
/* \x{..} / \o{..}: single underscores between digits are ignored; the first character that is no
 * digit (a second underscore, a trailing one, 'x') ends the number */
static void p5_put_numbrace(Potion *P, const char *t, int n, int base) {
  unsigned long v = 0;
  int i;
  for (i = 0; i < n; i++) {
    int d;
    char c = t[i];
    if (c == '_') {
      if (i + 1 < n && t[i + 1] != '_' && isxdigit((unsigned char)t[i + 1])) continue;
      break;
    }
    d = isdigit((unsigned char)c) ? c - '0' : isxdigit((unsigned char)c) ? (c | 0x20) - 'a' + 10 : 99;
    if (d >= base) break;
    v = v * base + d;
  }
  p5_put_cp(P, v);
}
static int p5_dq_marker(PN v) {
  if (PN_IS_PTR(v) && PN_VTYPE(v) == PN_TSOURCE && PN_PART(v) == AST_VALUE && PN_IS_STR(PN_S(v, 0)) && PN_STR_LEN(PN_S(v, 0)) == 2
      && PN_STR_PTR(PN_S(v, 0))[0] == 1) return PN_STR_PTR(PN_S(v, 0))[1];
  return 0;
}
static PN p5_dq_group(Potion *P, long lineno, PN out, PN_SIZE start, int kind, int one) {
  PN acc = PN_NIL, res = PN_TUP0();
  PN_SIZE i;
  for (i = 0; i < start; i++) res = PN_PUSH(res, PN_TUPLE_AT(out, i));
  for (i = start; i < PN_TUPLE_LEN(out); i++)
    acc = acc == PN_NIL ? PN_TUPLE_AT(out, i) : p5_cat2(P, lineno, P->line, acc, PN_TUPLE_AT(out, i));
  if (acc == PN_NIL) return res;
  acc = potion_source(P, AST_EXPR, PN_PUSH(PN_TUP(acc), potion_source(P, AST_MSG, PN_STR(kind == 'Q' ? "quotemeta" : kind == 'U' ? "uc" : "lc"),
                      PN_NIL, PN_NIL, lineno, P->line)), PN_NIL, PN_NIL, lineno, P->line);
  if (one) /* \u\L...: the first letter of the group's result */
    acc = potion_source(P, AST_EXPR, PN_PUSH(PN_TUP(acc), potion_source(P, AST_MSG, PN_STR(one == 'u' ? "ucfirst" : "lcfirst"), PN_NIL, PN_NIL, lineno, P->line)), PN_NIL, PN_NIL, lineno, P->line);
  return PN_PUSH(res, acc);
}
static PN p5_dq_finish(Potion *P, long lineno) {
  PN last = potion_source(P, AST_VALUE,
                          potion_bytes_string(P, PN_NIL, (PN)P->pbuf),
                          PN_NIL, PN_NIL, lineno, P->line);
  if (PN_TUPLE_LEN(P->dqpieces) == 0)
    return last;
  {
    PN items = PN_TUP0(), out = PN_TUP0(), acc;
    char kinds[16], gone[16];
    PN_SIZE starts[16];
    int sp = 0, i, c;
    for (i = 0; i < (int)PN_TUPLE_LEN(P->dqpieces); i++) items = PN_PUSH(items, PN_TUPLE_AT(P->dqpieces, i));
    items = PN_PUSH(items, last);
    int one = 0; /* a pending \u or \l: the next non empty piece */
    for (i = 0; i < (int)PN_TUPLE_LEN(items); i++) {
      PN it = PN_TUPLE_AT(items, i);
      c = p5_dq_marker(it);
      if (c == 'u' || c == 'l') { one = c; continue; }
      if (!c && one) {
        int empty = PN_IS_PTR(it) && PN_VTYPE(it) == PN_TSOURCE && PN_PART(it) == AST_VALUE && PN_IS_STR(PN_S(it, 0)) && PN_STR_LEN(PN_S(it, 0)) == 0;
        if (!empty) {
          it = potion_source(P, AST_EXPR, PN_PUSH(PN_TUP(it), potion_source(P, AST_MSG, PN_STR(one == 'u' ? "ucfirst" : "lcfirst"), PN_NIL, PN_NIL, lineno, P->line)), PN_NIL, PN_NIL, lineno, P->line);
          one = 0;
        }
      }
      if (c == 'E') {
        if (sp > 0) { sp--; out = p5_dq_group(P, lineno, out, starts[sp], kinds[sp], gone[sp]); }
      } else if (c) {
        if ((c == 'U' || c == 'L') && sp > 0 && (kinds[sp - 1] == 'U' || kinds[sp - 1] == 'L')) { sp--; out = p5_dq_group(P, lineno, out, starts[sp], kinds[sp], gone[sp]); }
        if (sp < 16) { kinds[sp] = (char)c; gone[sp] = (char)one; one = 0; starts[sp] = PN_TUPLE_LEN(out); sp++; }
      } else out = PN_PUSH(out, it);
    }
    while (sp > 0) { sp--; out = p5_dq_group(P, lineno, out, starts[sp], kinds[sp], gone[sp]); }
    if (PN_TUPLE_LEN(out) == 0) return last;
    acc = PN_TUPLE_AT(out, 0);
    for (i = 1; i < (int)PN_TUPLE_LEN(out); i++)
      acc = p5_cat2(P, lineno, P->line, acc, PN_TUPLE_AT(out, i));
    return acc;
  }
}

/* 'while (..) {..}' style statement. A helper (not an inline action) because
 * stmt has a variable named 'a', which the greg macros would expand inside
 * the '->a[]' member accesses. */
static PN p5_special_stmt(Potion *P, long lineno, PN m, PN l, PN b) {
  PN_SRC(m)->a[1] = PN_SRC(l); PN_TOUCH(m);
  PN_SRC(m)->a[2] = PN_SRC(b); PN_TOUCH(m);
  return potion_source(P, AST_EXPR, PN_TUP(m), PN_NIL, PN_NIL, lineno, P->line);
}

static PN p5_list_elem(Potion *P, long lineno, PN line, PN r, long i);
/* a literal 'undef' target in (undef, $a) = LIST skips that element */
static PN p5_pat_undef;
static int p5_undef_target(Potion *P, PN v) {
  PN caps[PN_AST_CAPS];
  { /* the bare `undef` value: EXPR (VALUE nil) */
    PN t = v;
    while (PN_IS_PTR(t) && PN_VTYPE(t) == PN_TSOURCE && PN_PART(t) == AST_EXPR && PN_IS_TUPLE(PN_S(t, 0)) && PN_TUPLE_LEN(PN_S(t, 0)) == 1) t = PN_TUPLE_AT(PN_S(t, 0), 0);
    if (PN_IS_PTR(t) && PN_VTYPE(t) == PN_TSOURCE && PN_PART(t) == AST_VALUE && PN_S(t, 0) == PN_NIL) return 1;
  }
  if (!p5_pat_undef) {
    p5_pat_undef = potion_source(P, AST_MSG, PN_STR("undef"), PN_NIL, potion_ast_wild(P, -1), 0, PN_NIL);
    potion_gc_root(P, &p5_pat_undef);
  }
  return potion_ast_match(P, p5_pat_undef, v, caps, 0);
}
/* '($a, $b) = (LIST)': the whole rhs is evaluated into a temporary array
 * first, so '($x, $y) = ($y, $x)' swaps. A lone array rhs (@_, @a) keeps the
 * direct element access of p5_list_elem. */
/* 'my (LIST) = RHS' as an expression (a condition): the value is the number of
 * RHS elements, e.g. while (my ($k, $v) = each %h). RHS must be a list. The
 * generated code is @tmp = RHS; $a = @tmp[0]; ...; @tmp.length */
/* the sigil of a slurpy list assignment target (%h or @a, possibly wrapped in an EXPR), else 0 */
static PN p5_pat_slurpy;
static int p5_slurpy_sigil(Potion *P, PN v) {
  PN caps[PN_AST_CAPS];
  if (!p5_pat_slurpy) { /* MSG(?0), seen through EXPR[..] wrappers */
    p5_pat_slurpy = potion_source(P, AST_MSG, potion_ast_wild(P, 0), potion_ast_wild(P, -1), potion_ast_wild(P, -1), 0, PN_NIL);
    potion_gc_root(P, &p5_pat_slurpy);
  }
  if (potion_ast_match(P, p5_pat_slurpy, v, caps, PN_AST_PEEL) && PN_IS_STR(caps[0]) && PN_STR_LEN(caps[0]) > 1 &&
      (PN_STR_PTR(caps[0])[0] == '%' || PN_STR_PTR(caps[0])[0] == '@'))
    return PN_STR_PTR(caps[0])[0];
  return 0;
}
/* element I of the temporary list NB (the whole rest of it for a slurpy %h / @a target) */
static PN p5_tmp_elem(Potion *P, long lineno, PN line, const char *nb, int nn, long i, long nt, PN v) {
  PN idx = potion_source(P, AST_VALUE, PN_NUM(i), PN_NIL, PN_NIL, lineno, line);
  PN tmpm = potion_source(P, AST_MSG, PN_STRN((char *)nb, nn), PN_NIL, PN_NIL, lineno, line);
  PN val;
  int slurpy = i == nt - 1 && p5_slurpy_sigil(P, v);
  if (slurpy) {
    val = potion_source(P, AST_EXPR,
            PN_PUSH(PN_TUP(tmpm), potion_source(P, AST_MSG, PN_STR("p5rest"),
                      potion_source(P, AST_LIST, PN_TUP(idx), PN_NIL, PN_NIL, lineno, line), PN_NIL, lineno, line)),
            PN_NIL, PN_NIL, lineno, line);
    if (p5_slurpy_sigil(P, v) == '%')
      val = potion_source(P, AST_EXPR, PN_PUSH(PN_TUP(val), potion_source(P, AST_MSG, PN_STR("table"), PN_NIL, PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line);
    return val;
  }
  return potion_source(P, AST_EXPR, PN_TUP(potion_source(P, AST_MSG, PN_STRN((char *)nb, nn),
           potion_source(P, AST_LIST, PN_TUP(idx), PN_NIL, PN_NIL, lineno, line), PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line);
}
static PN p5_aasign_count(Potion *P, long lineno, PN line, PN lhs, PN r) {
  static int ctr = 0;
  char nb[40];
  int nn = snprintf(nb, sizeof nb, "@__ac_%d", ctr++);
  PN items = PN_S(r, 0), s1 = PN_TUP0(), tmp, rhs;
#define P5_TMPMSG() potion_source(P, AST_MSG, PN_STRN(nb, nn), PN_NIL, PN_NIL, lineno, line)
  tmp = P5_TMPMSG();
  rhs = (PN_IS_TUPLE(items) && PN_TUPLE_LEN(items) == 1) ? p5_listctx(P, PN_TUPLE_AT(items, 0)) : r;
  s1 = PN_PUSH(s1, potion_source(P, AST_ASSIGN, tmp, rhs, PN_NIL, lineno, line));
  PN_TUPLE_EACH(PN_S(lhs, 0), i, v, {
    if (p5_undef_target(P, v)) continue;
    s1 = PN_PUSH(s1, potion_source(P, AST_ASSIGN, v,
            p5_tmp_elem(P, lineno, line, nb, nn, i, (long)PN_TUPLE_LEN(PN_S(lhs, 0)), v), PN_NIL, lineno, line));
  });
  s1 = PN_PUSH(s1, potion_source(P, AST_EXPR,
         PN_PUSH(PN_TUP(P5_TMPMSG()), potion_source(P, AST_MSG, PN_STR("length"), PN_NIL, PN_NIL, lineno, line)),
         PN_NIL, PN_NIL, lineno, line));
#undef P5_TMPMSG
  return potion_source(P, AST_EXPR, s1, PN_NIL, PN_NIL, lineno, line);
}

/* is ITEM a call (f(...), split ..., a method chain) whose value is a list? Needs 2+
 * targets, or a known list returning builtin for a single one */
static PN p5_pat_lc_expr, p5_pat_lc_msg;
static int p5_list_call(Potion *P, PN item, long ntargets) {
  PN caps[PN_AST_CAPS], items;
  if (!p5_pat_lc_expr) { /* EXPR[..] and MSG(?0 name, ?1 args) */
    p5_pat_lc_expr = potion_source(P, AST_EXPR, potion_ast_wild(P, 0), PN_NIL, PN_NIL, 0, PN_NIL); potion_gc_root(P, &p5_pat_lc_expr);
    p5_pat_lc_msg = potion_source(P, AST_MSG, potion_ast_wild(P, 0), potion_ast_wild(P, 1), potion_ast_wild(P, -1), 0, PN_NIL);
    potion_gc_root(P, &p5_pat_lc_msg);
  }
  if (!potion_ast_match(P, p5_pat_lc_expr, item, caps, 0) || !PN_IS_TUPLE(caps[0])) return 0;
  items = caps[0];
  if (PN_TUPLE_LEN(items) >= 2) {
    PN last = PN_TUPLE_AT(items, PN_TUPLE_LEN(items) - 1);
    if (PN_IS_PTR(last) && PN_VTYPE(last) == PN_TSOURCE && PN_PART(last) == AST_MSG && PN_IS_STR(PN_S(last, 0)) &&
        (!strcmp(PN_STR_PTR(PN_S(last, 0)), "p5matchl") || !strcmp(PN_STR_PTR(PN_S(last, 0)), "p5matchall"))) return 1; /* m// in list context */
    if (PN_IS_PTR(last) && PN_VTYPE(last) == PN_TSOURCE && PN_PART(last) == AST_MSG && PN_IS_STR(PN_S(last, 0))) { /* my ($first) = sort/map/grep/reverse ... */
      static const char *lops[] = { "p5sort", "p5map", "p5grep", "reverse", "keys", "values", "p5slice", "p5sliceof", "p5aref", "glob", 0 };
      int k;
      for (k = 0; lops[k]; k++) if (!strcmp(PN_STR_PTR(PN_S(last, 0)), lops[k])) return 1;
    }
    return ntargets >= 2;
  }
  if (!potion_ast_match(P, p5_pat_lc_msg, PN_TUPLE_AT(items, 0), caps, 0) || !PN_IS_STR(caps[0])) return 0;
  if (!strcmp(PN_STR_PTR(caps[0]), "split") || !strcmp(PN_STR_PTR(caps[0]), "p5each")) return 1;
  return caps[1] != PN_NIL && ntargets >= 2;
}
/* the statement form of p5_aasign_count (its trailing element count is harmless) */
#define p5_aasign_count_stmt p5_aasign_count

static PN p5_fix_assign(Potion *P, long lineno, PN line, PN n);
/* tie: the names (with sigil) of the variables a `tie` was seen for; their scalar reads
 * become p5tfetch(..), their assignments p5tstore(..), whole array/hash assignments p5tassign(..).
 * Elements and methods of tied arrays/hashes dispatch to the Tie object at run time. */
static char p5_tied_names[64][64];
static int p5_tied_n;
static void p5_tie_note(PN name) {
  int i;
  if (!PN_IS_STR(name) || PN_STR_LEN(name) >= 64) return;
  for (i = 0; i < p5_tied_n; i++) if (!strcmp(p5_tied_names[i], PN_STR_PTR(name))) return;
  if (p5_tied_n < 64) strcpy(p5_tied_names[p5_tied_n++], PN_STR_PTR(name));
}
static int p5_tied_name(PN name) {
  int i;
  if (PN_IS_STR(name) && (!strcmp(PN_STR_PTR(name), "%^H") || !strcmp(PN_STR_PTR(name), "%ENV") || !strcmp(PN_STR_PTR(name), "%Config") ||
                          !strcmp(PN_STR_PTR(name), "%INC")))
    return 1; /* the hints hash, %ENV, %Config, %INC: lobby globals, elements go through at/put */
  if (!p5_tied_n || !PN_IS_STR(name)) return 0;
  for (i = 0; i < p5_tied_n; i++) if (!strcmp(p5_tied_names[i], PN_STR_PTR(name))) return 1;
  return 0;
}
/* f(args): a call of a lobby function */
static PN p5_fcall(Potion *P, long lineno, PN line, const char *fn, PN args) {
  return potion_source(P, AST_EXPR, PN_TUP(potion_source(P, AST_MSG, PN_STR(fn),
           potion_source(P, AST_LIST, args, PN_NIL, PN_NIL, lineno, line), PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line);
}
static PN p5_var_msg(PN n) { /* the bare variable MSG of n, looking through a p5tfetch read wrapper */
  PN w = n;
  while (PN_IS_PTR(w) && PN_VTYPE(w) == PN_TSOURCE && PN_PART(w) == AST_EXPR && PN_IS_TUPLE(PN_S(w, 0)) && PN_TUPLE_LEN(PN_S(w, 0)) == 1)
    w = PN_TUPLE_AT(PN_S(w, 0), 0);
  if (PN_IS_PTR(w) && PN_VTYPE(w) == PN_TSOURCE && PN_PART(w) == AST_MSG && PN_IS_STR(PN_S(w, 0))) {
    if (!strcmp(PN_STR_PTR(PN_S(w, 0)), "p5tfetch") && PN_IS_PTR(PN_S(w, 1)) && PN_IS_TUPLE(PN_S(PN_S(w, 1), 0)) && PN_TUPLE_LEN(PN_S(PN_S(w, 1), 0)) == 1)
      return p5_var_msg(PN_TUPLE_AT(PN_S(PN_S(w, 1), 0), 0));
    if (PN_S(w, 1) == PN_NIL) return w;
  }
  return PN_NIL;
}
static PN p5_unparen(Potion *P, PN e);
static PN p5_list_items(Potion *P, long lineno, PN line, PN items);
/* tie VAR, CLASS, LIST: VAR = p5tie(sigil, CLASS, [LIST]); the value is the tie object (p5tobj) */
static PN p5_tie_ast(Potion *P, long lineno, PN line, PN var, PN cls, PN args) {
  PN name = PN_S(var, 0), kind, call, asg;
  kind = potion_source(P, AST_VALUE, potion_str(P, PN_STR_PTR(name)[0] == '@' ? "@" : PN_STR_PTR(name)[0] == '%' ? "%" : "$"), PN_NIL, PN_NIL, lineno, line);
  p5_tie_note(name);
  call = p5_fcall(P, lineno, line, "p5tie", PN_PUSH(PN_PUSH(PN_TUP(kind), p5_unparen(P, cls)),
           args == PN_NIL ? potion_source(P, AST_LIST, PN_NIL, PN_NIL, PN_NIL, lineno, line) : p5_list_items(P, lineno, line, args)));
  asg = potion_source(P, AST_EXPR, PN_TUP(potion_source(P, AST_ASSIGN, var, call, PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line);
  return p5_fcall(P, lineno, line, "p5tobj", PN_TUP(asg));
}
/* untie VAR: UNTIE/DESTROY, then the variable gets the plain empty value back */
static PN p5_untie_ast(Potion *P, long lineno, PN line, PN var) {
  PN call = p5_fcall(P, lineno, line, "p5untie", PN_TUP(var));
  return potion_source(P, AST_EXPR, PN_TUP(potion_source(P, AST_ASSIGN, var, call, PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line);
}
/* $a[i] / $h{k}: the element access of a variable; for a tied array or hash a message to the
 * Tie object (at / put), else the indexed variable the compiler turns into a tuple/table op */
static PN p5_tel(Potion *P, long lineno, PN line, PN name, PN idx) {
  if (!p5_tied_name(name)) return potion_source(P, AST_MSG, name, idx, PN_NIL, lineno, line);
  return potion_source(P, AST_EXPR, PN_PUSH(PN_TUP(potion_source(P, AST_MSG, name, PN_NIL, PN_NIL, lineno, line)),
           potion_source(P, AST_MSG, PN_STR("at"), idx, PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line);
}
/* @tied = LIST / %tied = LIST: CLEAR and STORE through the Tie object, which stays in the variable */
static PN p5_tie_aggr(Potion *P, long lineno, PN line, PN a) {
  PN n = a;
  if (!p5_tied_n) return a;
  while (PN_IS_PTR(n) && PN_VTYPE(n) == PN_TSOURCE && PN_PART(n) == AST_EXPR && PN_IS_TUPLE(PN_S(n, 0)) && PN_TUPLE_LEN(PN_S(n, 0)) == 1)
    n = PN_TUPLE_AT(PN_S(n, 0), 0);
  if (PN_IS_PTR(n) && PN_VTYPE(n) == PN_TSOURCE && PN_PART(n) == AST_ASSIGN) {
    PN l = PN_S(n, 0);
    if (PN_IS_PTR(l) && PN_VTYPE(l) == PN_TSOURCE && PN_PART(l) == AST_MSG && PN_S(l, 1) == PN_NIL && PN_IS_STR(PN_S(l, 0)) &&
        PN_STR_LEN(PN_S(l, 0)) > 1 && (PN_STR_PTR(PN_S(l, 0))[0] == '@' || PN_STR_PTR(PN_S(l, 0))[0] == '%') && p5_tied_name(PN_S(l, 0))) {
      PN lhs = potion_source(P, AST_MSG, PN_S(l, 0), PN_NIL, PN_NIL, lineno, line);
      PN rhs = p5_fcall(P, lineno, line, "p5tassign", PN_PUSH(PN_TUP(lhs), PN_S(n, 1)));
      PN_SRC(n)->a[1] = PN_SRC(rhs); PN_TOUCH(n);
    }
  }
  return a;
}
/* read(FH, $buf, LEN, OFF): $buf = FH.p5readbuf($buf, LEN, OFF), the value is the byte count */
static PN p5_read_ast(Potion *P, long lineno, PN line, PN fh, PN var, PN len, PN off) {
  PN bare = p5_var_msg(var), vm;
  if (bare == PN_NIL) bare = var;
  vm = potion_source(P, AST_MSG, PN_S(bare, 0), PN_NIL, PN_NIL, lineno, line);
  PN offv = off == PN_NIL ? potion_source(P, AST_VALUE, PN_NIL, PN_NIL, PN_NIL, lineno, line) : p5_unparen(P, off);
  PN call = potion_source(P, AST_EXPR, PN_PUSH(PN_TUP(fh), potion_source(P, AST_MSG, PN_STR("p5readbuf"),
              potion_source(P, AST_LIST, PN_PUSH(PN_PUSH(PN_TUP(vm), p5_unparen(P, len)), offv), PN_NIL, PN_NIL, lineno, line), PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line);
  PN asg = potion_source(P, AST_EXPR, PN_TUP(potion_source(P, AST_ASSIGN, vm, call, PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line);
  return p5_fcall(P, lineno, line, "p5readn", PN_TUP(asg));
}
/* a read of $name: p5tfetch($name) once the name has been tied */
static PN p5_sread(Potion *P, long lineno, PN line, PN name) {
  PN m = potion_source(P, AST_MSG, name, PN_NIL, PN_NIL, lineno, line);
  if (!p5_tied_name(name)) return m;
  return p5_fcall(P, lineno, line, "p5tfetch", PN_TUP(m));
}
static PN p5_copy_rhs(Potion *P, PN lhs, PN rhs);
static PN p5_readline_all(Potion *P, PN item);
static int p5_is_arrvar(PN n);
/* (COND ? $a : $b) = V: the ternary is an lvalue, V is assigned to the chosen branch */
static PN p5_ternary_assign(Potion *P, long lineno, PN line, PN item, PN v) {
  PN t = item, st, mif, mel, out = PN_TUP0();
  int k;
  while (PN_IS_PTR(t) && PN_VTYPE(t) == PN_TSOURCE && PN_PART(t) == AST_EXPR && PN_IS_TUPLE(PN_S(t, 0)) && PN_TUPLE_LEN(PN_S(t, 0)) == 1 &&
         PN_IS_PTR(PN_TUPLE_AT(PN_S(t, 0), 0)) && PN_VTYPE(PN_TUPLE_AT(PN_S(t, 0), 0)) == PN_TSOURCE && PN_PART(PN_TUPLE_AT(PN_S(t, 0), 0)) == AST_EXPR) t = PN_TUPLE_AT(PN_S(t, 0), 0);
  if (!(PN_IS_PTR(t) && PN_VTYPE(t) == PN_TSOURCE && PN_PART(t) == AST_EXPR && PN_IS_TUPLE(PN_S(t, 0)) && PN_TUPLE_LEN(PN_S(t, 0)) == 2)) return PN_NIL;
  st = PN_S(t, 0);
  mif = PN_TUPLE_AT(st, 0); mel = PN_TUPLE_AT(st, 1);
  if (!(PN_IS_PTR(mif) && PN_VTYPE(mif) == PN_TSOURCE && PN_PART(mif) == AST_MSG && PN_S(mif, 0) == PN_if && PN_IS_PTR(mel) && PN_VTYPE(mel) == PN_TSOURCE && PN_PART(mel) == AST_MSG && PN_S(mel, 0) == PN_else)) return PN_NIL;
  for (k = 0; k < 2; k++) {
    PN m = k == 0 ? mif : mel, blk = PN_SRC(m)->a[2] ? (PN)PN_SRC(m)->a[2] : PN_NIL, ex, tgt, asg;
    if (!(PN_IS_PTR(blk) && PN_VTYPE(blk) == PN_TSOURCE && PN_PART(blk) == AST_BLOCK && PN_IS_TUPLE(PN_S(blk, 0)) && PN_TUPLE_LEN(PN_S(blk, 0)) == 1)) return PN_NIL;
    ex = PN_TUPLE_AT(PN_S(blk, 0), 0);
    tgt = PN_IS_PTR(ex) && PN_VTYPE(ex) == PN_TSOURCE && PN_PART(ex) == AST_EXPR && PN_IS_TUPLE(PN_S(ex, 0)) && PN_TUPLE_LEN(PN_S(ex, 0)) == 1 ? PN_TUPLE_AT(PN_S(ex, 0), 0) : ex;
    asg = potion_source(P, AST_EXPR, PN_TUP(p5_fix_assign(P, lineno, line, potion_source(P, AST_ASSIGN, tgt, v, PN_NIL, lineno, line))), PN_NIL, PN_NIL, lineno, line);
    out = PN_PUSH(out, potion_source(P, AST_MSG, PN_S(m, 0), PN_S(m, 1), potion_source(P, AST_BLOCK, PN_TUP(asg), PN_NIL, PN_NIL, lineno, line), lineno, line));
  }
  return potion_source(P, AST_EXPR, out, PN_NIL, PN_NIL, lineno, line);
}
static PN p5_aasign(Potion *P, long lineno, PN line, PN lhs, PN r) {
  static int ctr = 0;
  if (PN_IS_TUPLE(PN_S(lhs, 0)) && PN_TUPLE_LEN(PN_S(lhs, 0)) == 1 && PN_IS_TUPLE(PN_S(r, 0)) && PN_TUPLE_LEN(PN_S(r, 0)) == 1) {
    PN ta = p5_ternary_assign(P, lineno, line, PN_TUPLE_AT(PN_S(lhs, 0), 0), PN_TUPLE_AT(PN_S(r, 0), 0));
    if (ta != PN_NIL) return ta;
  }
  if (!PN_IS_TUPLE(PN_S(lhs, 0)) || PN_TUPLE_LEN(PN_S(lhs, 0)) == 0) /* () = EXPR: only evaluate */
    return potion_source(P, AST_EXPR, PN_IS_TUPLE(PN_S(r, 0)) ? PN_S(r, 0) : PN_TUP0(), PN_NIL, PN_NIL, lineno, line);
  char nb[40];
  int nn = snprintf(nb, sizeof nb, "@__aa_%d", ctr++);
  PN items = PN_S(r, 0), s1 = PN_TUP0(), tmp;
  /* ($a, $b) = qw(x y): the qw word LIST is the rhs list itself */
  if (PN_IS_TUPLE(items) && PN_TUPLE_LEN(items) == 1) {
    PN it = PN_TUPLE_AT(items, 0);
    if (PN_IS_PTR(it) && PN_VTYPE(it) == PN_TSOURCE && PN_PART(it) == AST_EXPR && PN_IS_TUPLE(PN_S(it, 0)) &&
        PN_TUPLE_LEN(PN_S(it, 0)) == 1) {
      PN inner = PN_TUPLE_AT(PN_S(it, 0), 0);
      if (PN_IS_PTR(inner) && PN_VTYPE(inner) == PN_TSOURCE && PN_PART(inner) == AST_LIST) { r = inner; items = PN_S(r, 0); }
    }
  }
  /* A slurpy list/hash target takes the remaining elements wherever it appears;
   * targets after it receive undef. This covers local (@ARGV, $/) = PATH. */
  {
    long nt = PN_TUPLE_LEN(PN_S(lhs, 0)), slurpy = -1, i;
    for (i = 0; i < nt; i++)
      if (p5_slurpy_sigil(P, PN_TUPLE_AT(PN_S(lhs, 0), i))) { slurpy = i; break; }
    if (slurpy >= 0) {
      PN src = (PN_IS_TUPLE(items) && PN_TUPLE_LEN(items) == 1) ? PN_TUPLE_AT(items, 0) : r;
      tmp = potion_source(P, AST_MSG, PN_STRN(nb, nn), PN_NIL, PN_NIL, lineno, line);
      s1 = PN_PUSH(s1, potion_source(P, AST_ASSIGN, tmp, src, PN_NIL, lineno, line));
      for (i = 0; i < nt; i++) {
        PN v = PN_TUPLE_AT(PN_S(lhs, 0), i), value;
        if (p5_undef_target(P, v)) continue;
        if (i < slurpy)
          value = p5_tmp_elem(P, lineno, line, nb, nn, i, nt, v);
        else if (i == slurpy)
          value = p5_tmp_elem(P, lineno, line, nb, nn, i, i + 1, v);
        else
          value = potion_source(P, AST_VALUE, PN_NIL, PN_NIL, PN_NIL, lineno, line);
        s1 = PN_PUSH(s1, potion_source(P, AST_ASSIGN, v, value, PN_NIL, lineno, line));
      }
      return potion_source(P, AST_EXPR, s1, PN_NIL, PN_NIL, lineno, line);
    }
  }
  /* 'my ($a, $b) = f(...)' / split ...: one call item whose value is the list; scalar
   * values of a single variable keep the element-wise path below */
  if (PN_IS_TUPLE(items) && PN_TUPLE_LEN(items) == 1 && p5_list_call(P, PN_TUPLE_AT(items, 0),
        PN_IS_TUPLE(PN_S(lhs, 0)) ? (long)PN_TUPLE_LEN(PN_S(lhs, 0)) : 0))
    return p5_aasign_count_stmt(P, lineno, line, lhs, r);
  if (!PN_IS_TUPLE(items) || PN_TUPLE_LEN(items) < 2) {
    PN_TUPLE_EACH(PN_S(lhs, 0), i, v, {
    if (p5_undef_target(P, v)) continue;
      s1 = PN_PUSH(s1, potion_source(P, AST_ASSIGN, v, p5_list_elem(P, lineno, line, r, i),
                                      PN_NIL, lineno, line));
    });
    return potion_source(P, AST_EXPR, s1, PN_NIL, PN_NIL, lineno, line);
  }
  tmp = potion_source(P, AST_MSG, PN_STRN(nb, nn), PN_NIL, PN_NIL, lineno, line);
  s1 = PN_PUSH(s1, potion_source(P, AST_ASSIGN, tmp, r, PN_NIL, lineno, line));
  PN_TUPLE_EACH(PN_S(lhs, 0), i, v, {
    if (p5_undef_target(P, v)) continue;
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

static PN p5_flatten1(Potion *P, PN list_ast);
static PN p5_listctx(Potion *P, PN item);
/* @$r / @{EXPR}: the list REF points to (the reference is the tuple itself) */
static PN p5_aref(Potion *P, long lineno, PN line, PN node) {
  return potion_source(P, AST_EXPR,
           PN_PUSH(PN_TUP(node), potion_source(P, AST_MSG, PN_STR("p5aref"), PN_NIL, PN_NIL, lineno, line)),
           PN_NIL, PN_NIL, lineno, line);
}
/* map/grep/sort BLOCK LIST: EXPR[LIST, MSG p5xxx(block)] -- the list is the
 * receiver (a lone array is used directly, not nested) */
static PN p5_listop(Potion *P, long lineno, PN line, const char *op, PN block, PN items) {
  /* a single item (@a, (1,2,3), keys %h) already is the list; the natives wrap a
   * lone scalar themselves */
  PN lst;
  if (PN_IS_TUPLE(items)) { PN_SIZE li; for (li = 0; li < PN_TUPLE_LEN(items); li++) p5_listctx(P, PN_TUPLE_AT(items, li)); } /* the list of map/grep/sort is in list context */
  lst = (PN_IS_TUPLE(items) && PN_TUPLE_LEN(items) == 1)
    ? PN_TUPLE_AT(items, 0)
    : potion_source(P, AST_LIST, items, PN_NIL, PN_NIL, lineno, line);
  PN msg;
  if (block != PN_NIL && (!strcmp(op, "p5map") || !strcmp(op, "p5grep"))) {
    /* the block is a closure taking the element as $_: a `$_` the caller assigned (for, $_ = ..) is a local of the
     * enclosing proto that the natives cannot set, the parameter shadows it */
    PN proto = potion_source(P, AST_PROTO, potion_source(P, AST_LIST, PN_TUP(PN_STRN("$_", 2)), PN_NIL, PN_NIL, lineno, line), block, PN_NIL, lineno, line);
    msg = potion_source(P, AST_MSG, PN_STR(op), potion_source(P, AST_LIST, PN_TUP(potion_source(P, AST_EXPR, PN_TUP(proto), PN_NIL, PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line), PN_NIL, lineno, line);
    return potion_source(P, AST_EXPR, PN_PUSH(PN_TUP(lst), msg), PN_NIL, PN_NIL, lineno, line);
  }
  msg = block != PN_NIL
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
/* the current package ("" is main); `{ package Foo; ... }` ends with its block, a required
 * file starts in main */
static char p5_pkg[64];
static char p5_cursub[64]; /* the named sub being parsed: for next::method */
static char p5_pkg_stack[32][64];
static int p5_pkg_depth;
static void p5_pkg_save(void) {
  if (p5_pkg_depth < 32) strcpy(p5_pkg_stack[p5_pkg_depth], p5_pkg);
  p5_pkg_depth++;
}
static void p5_pkg_restore(void) {
  p5_pkg_depth--;
  if (p5_pkg_depth >= 0 && p5_pkg_depth < 32) strcpy(p5_pkg, p5_pkg_stack[p5_pkg_depth]);
}
static void p5_set_cursub(PN name) {
  const char *n = PN_STR_PTR(name), *c = strrchr(n, ':');
  if (c) n = c + 1;
  if (strlen(n) < 64) strcpy(p5_cursub, n);
}
static void p5_set_pkg(PN name) {
  if (PN_STR_LEN(name) == 4 && !memcmp(PN_STR_PTR(name), "main", 4)) p5_pkg[0] = 0; /* main is the unqualified package */
  else if (PN_STR_LEN(name) < 64) { memcpy(p5_pkg, PN_STR_PTR(name), PN_STR_LEN(name)); p5_pkg[PN_STR_LEN(name)] = 0; }
}
/* `state $n = 0;`: a variable that lives as long as the program: the global $__st_N_n, which the name $n
 * is mapped to until the end of the enclosing block (p5_vn consults the map) */
static void p5_viv_add(const char *name);
static char p5_state_keys[32][48], p5_state_globals[32][64];
static int p5_state_n, p5_state_marks[16], p5_state_ctr;
/* %^H and $^H are scoped by the enclosing block / file at parse (action) time, see p5hhpush in lib/p5/p5number.c */
static void p5_hh_save(Potion *P, int clear) { potion_send(P->lobby, PN_STR("p5hhpush"), clear ? PN_TRUE : PN_FALSE); }
static void p5_hh_restore(Potion *P) { potion_send(P->lobby, PN_STR("p5hhpop")); }
/* is the list item a call (f(..), stat(..), $o->m(..)): in (CALL)[i] its list result is spread */
static int p5_is_call_item(PN e) {
  while (PN_IS_PTR(e) && PN_VTYPE(e) == PN_TSOURCE && PN_PART(e) == AST_EXPR && PN_IS_TUPLE(PN_S(e, 0)) && PN_TUPLE_LEN(PN_S(e, 0)) >= 1)
    e = PN_TUPLE_AT(PN_S(e, 0), PN_TUPLE_LEN(PN_S(e, 0)) - 1);
  return PN_IS_PTR(e) && PN_VTYPE(e) == PN_TSOURCE && PN_PART(e) == AST_MSG && PN_IS_STR(PN_S(e, 0)) && PN_S(e, 1) != PN_NIL &&
         strcmp(PN_STR_PTR(PN_S(e, 0)), "at") && strcmp(PN_STR_PTR(PN_S(e, 0)), "p5sliceof");
}
/* eval STRING: a site with active compile time hints (%^H, $^H) passes the index of a snapshot, p5evalstrh compiles with it */
static PN p5_evalstr_ast(Potion *P, long lineno, PN line, PN e) {
  PN idx = potion_send(P->lobby, PN_STR("p5hhsnap")), msg;
  if (PN_IS_INT(idx))
    msg = potion_source(P, AST_MSG, PN_STR("p5evalstrh"), potion_source(P, AST_LIST,
            PN_TUP(potion_source(P, AST_VALUE, idx, PN_NIL, PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line), PN_NIL, lineno, line);
  else msg = potion_source(P, AST_MSG, PN_STR("p5evalstr"), PN_NIL, PN_NIL, lineno, line);
  return potion_source(P, AST_EXPR, PN_PUSH(PN_TUPIF(e), msg), PN_NIL, PN_NIL, lineno, line);
}
static void p5_local_blockstart(Potion *P) {
  p5_pkg_save();
  p5_hh_save(P, 0);
  if (p5_local_depth < 16) { p5_local_marks[p5_local_depth] = p5_local_n; p5_state_marks[p5_local_depth] = p5_state_n; }
  p5_local_depth++;
}
static PN p5_local_note(Potion *P, PN name) {
  static int rooted;
  if (!rooted) { /* the names are GC pointers in a static array: keep them updated */
    int i;
    for (i = 0; i < P5_LOCAL_MAX; i++) potion_gc_root(P, &p5_local_names[i]);
    rooted = 1;
  }
  /* a global hash element (%ENV, %^H: EXPR[MSG %h, MSG at(k)]) is saved like the plain element MSG %h(k) */
  if (PN_IS_PTR(name) && PN_VTYPE(name) == PN_TSOURCE && PN_PART(name) == AST_EXPR && PN_IS_TUPLE(PN_S(name, 0)) && PN_TUPLE_LEN(PN_S(name, 0)) == 2) {
    PN b = PN_TUPLE_AT(PN_S(name, 0), 0), a = PN_TUPLE_AT(PN_S(name, 0), 1);
    if (PN_IS_PTR(b) && PN_VTYPE(b) == PN_TSOURCE && PN_PART(b) == AST_MSG && PN_S(b, 1) == PN_NIL && PN_IS_PTR(a) && PN_VTYPE(a) == PN_TSOURCE &&
        PN_PART(a) == AST_MSG && PN_IS_STR(PN_S(a, 0)) && !strcmp(PN_STR_PTR(PN_S(a, 0)), "at"))
      name = potion_source(P, AST_MSG, PN_S(b, 0), PN_S(a, 1), PN_NIL, 0, PN_NIL);
  }
  if (p5_local_n < P5_LOCAL_MAX) p5_local_names[p5_local_n++] = name;
  return name;
}
static PN p5_vn(Potion *P, const char *sigil, PN name);
/* the variable assigned by the statement S of `local @a = ...`: unwrap EXPR[ASSIGN[MSG name, ..]] */
static PN p5_local_note_target(Potion *P, PN s) {
  while (PN_IS_PTR(s) && PN_PART(s) == AST_EXPR && PN_IS_TUPLE(PN_S(s, 0)) && PN_TUPLE_LEN(PN_S(s, 0)) == 1)
    s = PN_TUPLE_AT(PN_S(s, 0), 0);
  if (PN_IS_PTR(s) && PN_PART(s) == AST_ASSIGN) {
    PN t = PN_S(s, 0);
    while (PN_IS_PTR(t) && PN_PART(t) == AST_EXPR && PN_IS_TUPLE(PN_S(t, 0)) && PN_TUPLE_LEN(PN_S(t, 0)) == 1)
      t = PN_TUPLE_AT(PN_S(t, 0), 0);
    if (PN_IS_PTR(t) && PN_PART(t) == AST_MSG && PN_IS_STR(PN_S(t, 0))) return p5_local_note(P, PN_S(t, 0));
  }
  return PN_NIL;
}
/* Record every variable target of `local ($a, @b, %c)`. */
static void p5_local_note_list(Potion *P, PN list) {
  if (!PN_IS_PTR(list) || PN_PART(list) != AST_LIST || !PN_IS_TUPLE(PN_S(list, 0))) return;
  PN_TUPLE_EACH(PN_S(list, 0), i, v, {
    while (PN_IS_PTR(v) && PN_PART(v) == AST_EXPR && PN_IS_TUPLE(PN_S(v, 0)) && PN_TUPLE_LEN(PN_S(v, 0)) == 1)
      v = PN_TUPLE_AT(PN_S(v, 0), 0);
    if (PN_IS_PTR(v) && PN_PART(v) == AST_MSG && PN_IS_STR(PN_S(v, 0)))
      p5_local_note(P, PN_S(v, 0));
  });
}
/* state $n [= EXPR]; the first time through: the flag is set and the variable initialised */
static PN p5_state_decl(Potion *P, long lineno, PN line, const char *sigil, PN name, PN init) {
  char key[48], glob[64], flag[64];
  int id = p5_state_ctr++;
  PN gv, fv, set, body;
  snprintf(key, sizeof key, "%s%s", sigil, PN_STR_PTR(name));
  snprintf(glob, sizeof glob, "%s__st_%d_%s", sigil, id, PN_STR_PTR(name));
  snprintf(flag, sizeof flag, "$__si_%d", id);
  if (p5_state_n < 32) { strcpy(p5_state_keys[p5_state_n], key); strcpy(p5_state_globals[p5_state_n++], glob); }
  p5_viv_add(glob);
  p5_viv_add(flag);
  gv = potion_source(P, AST_MSG, potion_str(P, glob), PN_NIL, PN_NIL, lineno, line);
  fv = potion_source(P, AST_MSG, potion_str(P, flag), PN_NIL, PN_NIL, lineno, line);
  set = potion_source(P, AST_EXPR, PN_TUP(potion_source(P, AST_ASSIGN, fv, potion_source(P, AST_VALUE, PN_NUM(1), PN_NIL, PN_NIL, lineno, line), PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line);
  if (init == PN_NIL)
    init = sigil[0] == '@' ? potion_source(P, AST_LIST, PN_NIL, PN_NIL, PN_NIL, lineno, line)
         : sigil[0] == '%' ? potion_source(P, AST_EXPR, PN_PUSH(PN_TUP(potion_source(P, AST_LIST, PN_NIL, PN_NIL, PN_NIL, lineno, line)),
                               potion_source(P, AST_MSG, PN_STR("table"), PN_NIL, PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line)
         : potion_source(P, AST_VALUE, PN_NIL, PN_NIL, PN_NIL, lineno, line);
  body = potion_source(P, AST_BLOCK, PN_PUSH(PN_TUP(set), potion_source(P, AST_EXPR, PN_TUP(potion_source(P, AST_ASSIGN, gv, init, PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line)),
                       PN_NIL, PN_NIL, lineno, line);
  return potion_source(P, AST_EXPR, PN_TUP(potion_source(P, AST_MSG, PN_if,
           potion_source(P, AST_LIST, PN_TUP(potion_source(P, AST_NOT, fv, PN_NIL, PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line), body, lineno, line)),
         PN_NIL, PN_NIL, lineno, line);
}
/* local @a / local %h without a value: an empty list or hash */
static PN p5_local_empty(Potion *P, long lineno, PN line, PN var) {
  PN init = PN_STR_PTR(PN_S(var, 0))[0] == '@'
    ? potion_source(P, AST_LIST, PN_NIL, PN_NIL, PN_NIL, lineno, line)
    : potion_source(P, AST_EXPR, PN_PUSH(PN_TUP(potion_source(P, AST_LIST, PN_NIL, PN_NIL, PN_NIL, lineno, line)),
        potion_source(P, AST_MSG, PN_STR("table"), PN_NIL, PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line);
  return potion_source(P, AST_ASSIGN, var, init, PN_NIL, lineno, line);
}
static void p5_viv_scalar(PN name);
static PN p5_local_note_of(Potion *P, PN scalar_ast) { p5_viv_scalar(PN_S(scalar_ast, 0)); return p5_local_note(P, PN_S(scalar_ast, 0)); }
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
/* $^H (perl's compile time hints) is the native p5hints(): a read is p5hints(-1),
 * an assignment p5hints(VALUE). 'use integer' (HINT_INTEGER, 0x1) is
 * '$^H |= 1', recorded as the pseudo local "$^H": the enclosing block saves it
 * at its start and restores it at its end. */
static int p5_is_hint(PN name) { return PN_IS_STR(name) && !strcmp(PN_STR_PTR(name), "$^H"); }
static PN p5_hints_call(Potion *P, long lineno, PN line, PN arg) {
  PN args = potion_source(P, AST_LIST, PN_TUP(arg), PN_NIL, PN_NIL, lineno, line);
  return potion_source(P, AST_EXPR,
           PN_TUP(potion_source(P, AST_MSG, PN_STR("p5hints"), args, PN_NIL, lineno, line)),
           PN_NIL, PN_NIL, lineno, line);
}
static PN p5_hints_read(Potion *P, long lineno, PN line) {
  return p5_hints_call(P, lineno, line,
           potion_source(P, AST_VALUE, PN_NUM(-1), PN_NIL, PN_NIL, lineno, line));
}
/* is NODE the read of $^H (p5hints(-1))? */
static PN p5_pat_hints;
static int p5_is_hints_read(Potion *P, PN n) {
  PN caps[PN_AST_CAPS];
  if (!p5_pat_hints) { /* MSG(p5hints, args), the expr rules may wrap it in further single-item EXPRs */
    p5_pat_hints = potion_source(P, AST_MSG, PN_STR("p5hints"), potion_ast_some(P, -1), potion_ast_wild(P, -1), 0, PN_NIL);
    potion_gc_root(P, &p5_pat_hints);
  }
  return PN_IS_PTR(n) && PN_VTYPE(n) == PN_TSOURCE && PN_PART(n) == AST_EXPR &&
         potion_ast_match(P, p5_pat_hints, n, caps, PN_AST_PEEL);
}
/* $$r / ${$r}: EXPR[ $r, MSG p5deref ]; \$x: p5mkref(sub { $x }) (see lib/p5/p5ref.c) */
static PN p5_deref(Potion *P, long lineno, PN line, PN refvar) {
  return potion_source(P, AST_EXPR,
           PN_PUSH(PN_TUP(refvar), potion_source(P, AST_MSG, PN_STR("p5deref"), PN_NIL, PN_NIL, lineno, line)),
           PN_NIL, PN_NIL, lineno, line);
}
static PN p5_scalar_ref(Potion *P, long lineno, PN line, PN var);
/* the array/hash reference itself is the container: ${EXPR}[0] is EXPR->[0] */
static PN p5_deref_ref(Potion *P, PN e) { return p5_unparen(P, e); }
/* E->(ARGS) / &{E}(ARGS) for any expression E: E.p5callv((ARGS)) (lib/p5/p5table.c) */
static PN p5_callv_node(Potion *P, long lineno, PN line, PN e, PN l) {
  return potion_source(P, AST_EXPR,
           PN_PUSH(PN_TUP(e), potion_source(P, AST_MSG, PN_STR("p5callv"),
             potion_source(P, AST_LIST, PN_TUP(potion_source(P, AST_EXPR, PN_TUP(l), PN_NIL, PN_NIL, lineno, line)),
                           PN_NIL, PN_NIL, lineno, line), PN_NIL, lineno, line)),
           PN_NIL, PN_NIL, lineno, line);
}
/* the reference of a $$r node (unwrapping single item EXPRs), else NIL */
static PN p5_deref_target(PN n) {
  PN st, m;
  for (;;) {
    if (!PN_IS_PTR(n) || PN_VTYPE(n) != PN_TSOURCE || PN_PART(n) != AST_EXPR) return PN_NIL;
    st = PN_S(n, 0);
    if (!PN_IS_TUPLE(st)) return PN_NIL;
    if (PN_TUPLE_LEN(st) == 1) {
      m = PN_TUPLE_AT(st, 0);
      if (PN_IS_PTR(m) && PN_VTYPE(m) == PN_TSOURCE && PN_PART(m) == AST_EXPR) { n = m; continue; }
      return PN_NIL;
    }
    if (PN_TUPLE_LEN(st) != 2) return PN_NIL;
    m = PN_TUPLE_AT(st, 1);
    if (PN_IS_PTR(m) && PN_VTYPE(m) == PN_TSOURCE && PN_PART(m) == AST_MSG && PN_IS_STR(PN_S(m, 0)) &&
        !strcmp(PN_STR_PTR(PN_S(m, 0)), "p5deref"))
      return PN_TUPLE_AT(st, 0);
    return PN_NIL;
  }
}
/* '$^H = EXPR' => p5hints(EXPR); anything else unchanged */
/* $n = split ...: split in scalar context returns the number of fields */
static PN p5_split_scalar(Potion *P, long lineno, PN line, PN l, PN e) {
  PN n = e, m;
  if (!(PN_IS_PTR(l) && PN_VTYPE(l) == PN_TSOURCE && PN_PART(l) != AST_EXPR && PN_IS_STR(PN_S(l, 0)) &&
        PN_STR_PTR(PN_S(l, 0))[0] == '$')) return e;
  if (!(PN_IS_PTR(n) && PN_VTYPE(n) == PN_TSOURCE && PN_PART(n) == AST_EXPR && PN_IS_TUPLE(PN_S(n, 0)) &&
        PN_TUPLE_LEN(PN_S(n, 0)) == 1)) return e;
  m = PN_TUPLE_AT(PN_S(n, 0), 0);
  if (PN_IS_PTR(m) && PN_VTYPE(m) == PN_TSOURCE && PN_PART(m) == AST_MSG && PN_IS_STR(PN_S(m, 0)) &&
      !strcmp(PN_STR_PTR(PN_S(m, 0)), "split"))
    return potion_source(P, AST_EXPR,
             PN_PUSH(PN_TUP(e), potion_source(P, AST_MSG, PN_STR("length"), PN_NIL, PN_NIL, lineno, line)),
             PN_NIL, PN_NIL, lineno, line);
  return e;
}
/* named subs defined so far: a bareword with such a name is a call without arguments (perl:
 * `foo;` and `$x = foo;` after `sub foo {...}`), else a plain name */
static char p5_subnames[512][32];
static int p5_subnames_n;
static int p5_is_subname(const char *name, long len) {
  int i;
  for (i = 0; i < p5_subnames_n; i++)
    if (!strncmp(p5_subnames[i], name, len) && p5_subnames[i][len] == 0) return 1;
  return 0;
}
static void p5_note_subc(const char *name, long len) {
  if (len > 0 && len < 32 && p5_subnames_n < 512 && !p5_is_subname(name, len)) {
    memcpy(p5_subnames[p5_subnames_n], name, len);
    p5_subnames[p5_subnames_n++][len] = 0;
  }
}
static char p5_presubs[128][32];
static int p5_presubs_n;
static void p5_note_presub(const char *name, long len) {
  if (len > 0 && len < 32 && p5_presubs_n < 128) { memcpy(p5_presubs[p5_presubs_n], name, len); p5_presubs[p5_presubs_n++][len] = 0; }
}
static int p5_is_presub(const char *name, long len) {
  int i;
  for (i = 0; i < p5_presubs_n; i++) if (!strncmp(p5_presubs[i], name, len) && p5_presubs[i][len] == 0) return 1;
  return 0;
}
static int p5_sub_defs; /* named subs defined so far (in source order) */
static void p5_note_sub(PN name) { p5_sub_defs++; p5_note_subc(PN_STR_PTR(name), PN_STR_LEN(name)); }
/* a line-wise pre-scan of the source for `package NAME` and `sub NAME`, so that a sub of a
 * package can be called (f(), f) before its definition: the names are known when parsing the call */
/* subs with a (&...) prototype: `name BLOCK LIST` calls name(sub BLOCK, LIST) (noted by the pre-scan) */
static char p5_blocksubs[64][32];
static int p5_blocksubs_n;
static int p5_blocksub(const char *name, long len) {
  int i;
  for (i = 0; i < p5_blocksubs_n; i++)
    if (!strncmp(p5_blocksubs[i], name, len) && p5_blocksubs[i][len] == 0) return 1;
  return 0;
}
static void p5_prescan_subs(const char *src, long n) {
  char pkg[64];
  long i = 0;
  pkg[0] = 0;
  while (i < n) {
    long e = i, k;
    int pk = 0;
    while (e < n && src[e] != '\n') e++;
    k = i;
    while (k < e && (src[k] == ' ' || src[k] == '\t')) k++;
    if (e - k > 8 && !strncmp(src + k, "package", 7) && (src[k + 7] == ' ' || src[k + 7] == '\t')) pk = 8;
    else if (e - k > 4 && !strncmp(src + k, "sub", 3) && (src[k + 3] == ' ' || src[k + 3] == '\t')) pk = 4;
    if (pk) {
      long b = k + pk, f;
      while (b < e && (src[b] == ' ' || src[b] == '\t')) b++;
      f = b;
      while (f < e && (isalnum((unsigned char)src[f]) || src[f] == '_' || src[f] == ':')) f++;
      if (pk == 4 && f > b && f - b < 32 && p5_blocksubs_n < 64) { /* sub NAME (&...) */
        long q = f;
        while (q < e && (src[q] == ' ' || src[q] == '\t')) q++;
        if (q + 1 < e && src[q] == '(' && src[q + 1] == '&' && !p5_blocksub(src + b, f - b)) {
          memcpy(p5_blocksubs[p5_blocksubs_n], src + b, f - b);
          p5_blocksubs[p5_blocksubs_n++][f - b] = 0;
        }
      }
      if (f > b && f - b < 40) {
        if (pk == 8) {
          if (f - b == 4 && !strncmp(src + b, "main", 4)) pkg[0] = 0;
          else { memcpy(pkg, src + b, f - b); pkg[f - b] = 0; }
        } else if (!pkg[0]) {
          p5_note_presub(src + b, f - b); /* a main sub may be called f(@_) before its definition: the arguments are spread */
        } else if (pkg[0] && !memchr(src + b, ':', f - b)) {
          char q[96];
          int l = snprintf(q, sizeof q, "%s::%.*s", pkg, (int)(f - b), src + b);
          p5_note_subc(q, l);
        }
      }
    }
    i = e + 1;
  }
}
static PN p5_qname(Potion *P, PN n);
static int p5_macro_call(Potion *P, long lineno, PN line, PN m, PN args);
/* the bareword c (a one MSG tuple): a call of a known sub */
static PN p5_pad_args(Potion *P, PN name, PN items);
static PN p5_bare_call(Potion *P, long lineno, PN line, PN c) {
  PN m = PN_TUPLE_AT(c, 0);
  if (PN_S(m, 1) == PN_NIL && p5_macro_call(P, lineno, line, m, PN_NIL))
    return potion_source(P, AST_EXPR, c, PN_NIL, PN_NIL, lineno, line);
  if (PN_IS_STR(PN_S(m, 0)) && !strcmp(PN_STR_PTR(PN_S(m, 0)), "__PACKAGE__"))
    return potion_source(P, AST_VALUE, potion_str(P, p5_pkg[0] ? p5_pkg : "main"), PN_NIL, PN_NIL, lineno, line);
  if (p5_pkg[0] && PN_S(m, 1) == PN_NIL && PN_IS_STR(PN_S(m, 0)) && !strstr(PN_STR_PTR(PN_S(m, 0)), "::")) {
    PN qn = p5_qname(P, PN_S(m, 0));
    if (p5_is_subname(PN_STR_PTR(qn), PN_STR_LEN(qn))) { PN_SRC(m)->a[0] = PN_SRC(qn); PN_TOUCH(m); }
  }
  if (PN_S(m, 1) == PN_NIL && PN_IS_STR(PN_S(m, 0)) && !strcmp(PN_STR_PTR(PN_S(m, 0)), "unlink")) { /* unlink; works on $_: all nil arguments */
    PN_SRC(m)->a[1] = PN_SRC(potion_source(P, AST_LIST, p5_pad_args(P, m, PN_TUP(potion_source(P, AST_EXPR, PN_TUP(potion_source(P, AST_MSG, PN_STR("$_"), PN_NIL, PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line))), PN_NIL, PN_NIL, lineno, line)); PN_TOUCH(m);
  }
  if (PN_S(m, 1) == PN_NIL && PN_IS_STR(PN_S(m, 0)) && p5_is_subname(PN_STR_PTR(PN_S(m, 0)), PN_STR_LEN(PN_S(m, 0)))) {
    PN_SRC(m)->a[1] = PN_SRC(potion_source(P, AST_LIST, PN_NIL, PN_NIL, PN_NIL, lineno, line)); PN_TOUCH(m);
  }
  return potion_source(P, AST_EXPR, c, PN_NIL, PN_NIL, lineno, line);
}
/* package NAME { ... }: the block registers the package first */
static PN p5_pkg_block(Potion *P, long lineno, PN line, PN name, PN block) {
  PN stmts = PN_S(block, 0), ns, reg;
  PN_SIZE i;
  reg = potion_source(P, AST_EXPR, PN_TUP(potion_source(P, AST_MSG, PN_STR("p5pkg"),
          potion_source(P, AST_LIST, PN_TUP(potion_source(P, AST_VALUE, name, PN_NIL, PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line),
          PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line);
  ns = PN_TUP(reg);
  if (PN_IS_TUPLE(stmts)) for (i = 0; i < PN_TUPLE_LEN(stmts); i++) ns = PN_PUSH(ns, PN_TUPLE_AT(stmts, i));
  PN_SRC(block)->a[0] = PN_SRC(ns); PN_TOUCH(block);
  return block;
}
/* package variables: `our $x` in package Foo makes later $x, @x.. of that package Foo::x */
static char p5_ours[256][64];
static int p5_ours_n, p5_in_our;
static int p5_env_used; /* the program mentions %ENV/%Config: its code starts with p5envinit(), also when it runs compiled */
static void p5_env_touch(Potion *P) {
  if (!p5_env_used) { p5_env_used = 1; potion_send(P->lobby, PN_STR("p5envinit")); }
}
/* ---- the variables of the p2 -d loop ----
 * `$P2::repl` set: the parse of an eval string (<eval>) in the main package maps every variable that the input does
 * not declare with `my` to the lobby global `$REPL::name` (`@REPL::name`, `%REPL::name`). The compiler stores such
 * names with OP_GLOBAL (core/compile.c), so they live from input to input, subs and closures see the same variable and
 * `local` works. A `my` at the top level of the input declares the session variable, a `my` in a block, a foreach `my`
 * and a sub's `my` are real lexicals. */
static int p5_repl;
static int p5_in_my; /* 1: a name follows `my`, 2: `my (`: the names up to the = or ;, 3: `for my`: always a lexical */
static char p5_rdecl[128][64];
static int p5_rdecl_n;
static void p5_see(const char *name, long len);
static int p5_is_session(PN name) {
  return PN_IS_STR(name) && PN_STR_LEN(name) > 7 && !strncmp(PN_STR_PTR(name) + 1, "REPL::", 6);
}
/* the names that stay what they are: the topic, the sort variables, the standard handles and tables, argv */
static int p5_repl_special(const char *sigil, const char *nm) {
  static const char *skip[] = { "_", "ENV", "ARGV", "INC", "SIG", "STDIN", "STDOUT", "STDERR", "ARGVOUT", "Config", "0", NULL };
  int i;
  if (!(isalpha((unsigned char)nm[0]) || nm[0] == '_')) return 1; /* $1, $&, the punctuation variables */
  for (i = 0; skip[i]; i++) if (!strcmp(nm, skip[i])) return 1;
  return sigil[0] == '$' && nm[1] == '\0' && (nm[0] == 'a' || nm[0] == 'b');
}
/* is E a session variable read: MSG $REPL::x, maybe wrapped in a one element EXPR */
static int p5_sess_var(PN e) {
  while (PN_IS_PTR(e) && PN_VTYPE(e) == PN_TSOURCE && PN_PART(e) == AST_EXPR && PN_IS_TUPLE(PN_S(e, 0)) && PN_TUPLE_LEN(PN_S(e, 0)) == 1)
    e = PN_TUPLE_AT(PN_S(e, 0), 0);
  return PN_IS_PTR(e) && PN_VTYPE(e) == PN_TSOURCE && PN_PART(e) == AST_MSG && PN_S(e, 1) == PN_NIL && p5_is_session(PN_S(e, 0));
}
static PN p5_vn(Potion *P, const char *sigil, PN name) {
  char buf[160];
  const char *nm;
  int len;
  /* the lobby tables are built on first use; they allocate, so the name pointer is taken after it */
  if (sigil[0] == '%' && (!strcmp(PN_STR_PTR(name), "ENV") || !strcmp(PN_STR_PTR(name), "Config"))) p5_env_touch(P);
  nm = PN_STR_PTR(name);
  {
    int i;
    char key[64];
    snprintf(key, sizeof key, "%s%s", sigil, nm);
    for (i = p5_state_n - 1; i >= 0; i--)
      if (!strcmp(p5_state_keys[i], key)) return potion_str(P, p5_state_globals[i]);
  }
  if (p5_repl && !p5_pkg[0] && !strstr(nm, "::") && !p5_repl_special(sigil, nm)) {
    char key[64];
    int i, decl = 0, kl = snprintf(key, sizeof key, "%s%s", sigil, nm);
    if (kl < 64) {
      if (p5_in_my) { /* the name after `my`: a lexical in a block, the session variable at the top level of the input */
        if (p5_in_my == 3 || p5_local_depth > 1) { /* depth 1 is the input itself (repl-mark) */
          if (p5_rdecl_n < 128) strcpy(p5_rdecl[p5_rdecl_n++], key);
          decl = 1;
        }
        if (p5_in_my != 2) p5_in_my = 0;
      }
      for (i = 0; i < p5_rdecl_n && !decl; i++) if (!strcmp(p5_rdecl[i], key)) decl = 1;
      if (!decl) {
        char full[170];
        int fl = snprintf(full, sizeof full, "%sREPL::%s", sigil, nm);
        PN k;
        p5_in_our = 0;
        k = potion_str2(P, full, fl); /* nm is not used after this allocation */
        if (sigil[0] != '$') { /* an array or hash exists from its first mention */
          if (potion_bind(P, P->lobby, k) == PN_NIL)
            potion_define_global(P, k, sigil[0] == '@' ? PN_TUP0() : potion_table_empty(P));
          p5_see(full, fl);
        }
        return k;
      }
    }
  }
  if (p5_pkg[0] && !strstr(nm, "::") && strlen(nm) < 60) {
    char key[160];
    int i, kl = snprintf(key, sizeof key, "%s%s::%s", sigil, p5_pkg, nm);
    if (kl < 64) {
      for (i = 0; i < p5_ours_n; i++) if (!strcmp(p5_ours[i], key)) break;
      if (i == p5_ours_n && p5_in_our && p5_ours_n < 256) { strcpy(p5_ours[p5_ours_n++], key); p5_in_our = 0; }
      else if (i == p5_ours_n) i = -1;
      if (i >= 0) return potion_str2(P, key, kl);
    }
  }
  if (!strncmp(nm, "main::", 6) && nm[6]) nm += 6; /* $main::x is the global $x */
  else if (nm[0] == ':' && nm[1] == ':' && nm[2]) nm += 2;
  len = snprintf(buf, sizeof buf, "%s%s", sigil, nm);
  return potion_str2(P, buf, len);
}
/* NAME qualified with the current package (a name with :: stays) */
static PN p5_qname(Potion *P, PN n) {
  if (!p5_pkg[0] || strstr(PN_STR_PTR(n), "::")) return n;
  {
    char buf[160];
    int len = snprintf(buf, sizeof buf, "%s::%s", p5_pkg, PN_STR_PTR(n));
    return potion_str2(P, buf, len); /* interned: the runtime tables are keyed by identity */
  }
}
static PN p5_str_value(Potion *P, PN s) { return potion_source(P, AST_VALUE, s, PN_NIL, PN_NIL, 0, PN_NIL); }
/* sub NAME {..} in a package or sub Pkg::NAME: the closure is also registered under its
 * qualified name (p5defsub) for method resolution */
static PN p5_sub_def(Potion *P, long lineno, PN line, PN name, PN proto) {
  PN qn = p5_qname(P, name);
  PN rhs = potion_source(P, AST_EXPR, PN_TUP(proto), PN_NIL, PN_NIL, lineno, line);
  PN lhs;
  p5_note_sub(qn);
  lhs = potion_source(P, AST_EXPR, PN_TUP(potion_source(P, AST_MSG, qn, PN_NIL, PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line);
  if (!strstr(PN_STR_PTR(qn), "::")) {
    /* a sub of main: also registered as main::NAME for symbolic calls (&{"name"}(..)) */
    rhs = potion_source(P, AST_EXPR, PN_TUP(potion_source(P, AST_MSG, PN_STR("p5defsub"),
            potion_source(P, AST_LIST, PN_PUSH(PN_TUP(p5_str_value(P, PN_STRCAT("main::", PN_STR_PTR(qn)))), rhs), PN_NIL, PN_NIL, lineno, line),
            PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line);
    return potion_source(P, AST_ASSIGN, lhs, rhs, PN_NIL, lineno, line);
  }
  lhs = potion_source(P, AST_EXPR, PN_TUP(potion_source(P, AST_MSG, qn, PN_NIL, PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line);
  rhs = potion_source(P, AST_EXPR, PN_TUP(potion_source(P, AST_MSG, PN_STR("p5defsub"),
          potion_source(P, AST_LIST, PN_PUSH(PN_TUP(p5_str_value(P, qn)), rhs), PN_NIL, PN_NIL, lineno, line),
          PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line);
  return potion_source(P, AST_ASSIGN, lhs, rhs, PN_NIL, lineno, line);
}
/* exists &name / defined &name: the CODE slot of the glob is not undef */
static PN p5_sub_exists(Potion *P, long lineno, PN line, PN name) {
  PN slot = potion_source(P, AST_EXPR, PN_TUP(potion_source(P, AST_MSG, PN_STR("p5globslot"),
              potion_source(P, AST_LIST, PN_PUSH(PN_PUSH(PN_TUP(p5_str_value(P, name)), p5_str_value(P, PN_STR(""))), p5_str_value(P, PN_STR("CODE"))), PN_NIL, PN_NIL, lineno, line),
              PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line);
  return potion_source(P, AST_NEQ, slot, potion_source(P, AST_VALUE, PN_NIL, PN_NIL, PN_NIL, lineno, line), PN_NIL, lineno, line);
}
/* *NAME = VALUE: p5globset(name, pkg, value); NAME is an ast (a string value or a computed name) */
static PN p5_globset_ast(Potion *P, long lineno, PN line, PN name, PN pkg, PN value) {
  return potion_source(P, AST_MSG, PN_STR("p5globset"),
           potion_source(P, AST_LIST, PN_PUSH(PN_PUSH(PN_TUP(name), p5_str_value(P, pkg)), value), PN_NIL, PN_NIL, lineno, line),
           PN_NIL, lineno, line);
}
/* the glob object of NAME */
static PN p5_glob_ast(Potion *P, long lineno, PN line, PN name, PN pkg) {
  return potion_source(P, AST_EXPR, PN_TUP(potion_source(P, AST_MSG, PN_STR("p5glob"),
           potion_source(P, AST_LIST, PN_PUSH(PN_TUP(name), p5_str_value(P, pkg)), PN_NIL, PN_NIL, lineno, line),
           PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line);
}
/* `our @a;` / `our %h;` declares: an existing value stays, else the variable is an empty array / hash */
static PN p5_our_init(Potion *P, long lineno, PN line, PN var, int hash) {
  PN rd = potion_source(P, AST_MSG, PN_S(var, 0), PN_NIL, PN_NIL, lineno, line);
  PN call = potion_source(P, AST_EXPR, PN_TUP(potion_source(P, AST_MSG, PN_STR("p5our"),
              potion_source(P, AST_LIST, PN_PUSH(PN_TUP(rd), potion_source(P, AST_VALUE, hash ? PN_TRUE : PN_FALSE, PN_NIL, PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line),
              PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line);
  return potion_source(P, AST_ASSIGN, var, call, PN_NIL, lineno, line);
}
/* a call of the sub NAME written in a package: the package's own sub if there is one */
static PN p5_qualify_call(Potion *P, PN m) {
  if (p5_pkg[0] && PN_S(m, 1) == PN_NIL && PN_IS_STR(PN_S(m, 0)) && !strstr(PN_STR_PTR(PN_S(m, 0)), "::")) {
    PN qn = p5_qname(P, PN_S(m, 0));
    if (p5_is_subname(PN_STR_PTR(qn), PN_STR_LEN(qn))) { PN_SRC(m)->a[0] = PN_SRC(qn); PN_TOUCH(m); }
  }
  return m;
}
/* $o->name(ARGS) steps of a method chain: EXPR[RECV, p5meth("name", (ARGS)), ...]; a bareword
 * Class receiver is the class name; SUPER::name resolves in the parents of the current package */
static PN p5_meth_msg(Potion *P, PN st) {
  PN args = PN_S(st, 1);
  PN l = args != PN_NIL ? args : potion_source(P, AST_LIST, PN_NIL, PN_NIL, PN_NIL, 0, PN_NIL);
  PN name = PN_STR_LEN(PN_S(st, 0)) == 5 && !strcmp(PN_STR_PTR(PN_S(st, 0)), "p5dyn")
    ? potion_source(P, AST_EXPR, PN_TUP(PN_S(st, 2)), PN_NIL, PN_NIL, 0, PN_NIL) : p5_str_value(P, PN_S(st, 0));
  return potion_source(P, AST_MSG, PN_STR("p5meth"),
           potion_source(P, AST_LIST, PN_PUSH(PN_TUP(name),
                           potion_source(P, AST_EXPR, PN_TUP(l), PN_NIL, PN_NIL, 0, PN_NIL)), PN_NIL, PN_NIL, 0, PN_NIL),
           PN_NIL, 0, PN_NIL);
}
static PN p5_meth_recv(Potion *P, PN v) {
  if (PN_IS_PTR(v) && PN_VTYPE(v) == PN_TSOURCE && PN_PART(v) == AST_MSG && PN_S(v, 1) == PN_NIL && PN_IS_STR(PN_S(v, 0))) {
    const char *nm = PN_STR_PTR(PN_S(v, 0));
    if ((isupper((unsigned char)nm[0]) || (nm[0] & 0x80) || strstr(nm, "::")) && !p5_is_subname(nm, PN_STR_LEN(PN_S(v, 0))))
      return p5_str_value(P, PN_S(v, 0)); /* Class->method */
  }
  return v;
}
static PN p5_meth_step(Potion *P, PN chain, PN st) {
  char nmb[128]; /* a copy: the collector moves strings, a raw PN_STR_PTR dangles after the allocations below */
  const char *nm = nmb;
  snprintf(nmb, sizeof nmb, "%.*s", (int)PN_STR_LEN(PN_S(st, 0)), PN_STR_PTR(PN_S(st, 0)));
  if (!strcmp(nm, "p5callstep")) /* ->(args): the previous value is code */
    return PN_PUSH(chain, potion_source(P, AST_MSG, PN_STR("p5callv"),
             potion_source(P, AST_LIST, PN_TUP(potion_source(P, AST_EXPR, PN_TUP(PN_S(st, 1)), PN_NIL, PN_NIL, 0, PN_NIL)), PN_NIL, PN_NIL, 0, PN_NIL),
             PN_NIL, 0, PN_NIL));
  if (!strcmp(nm, "next::method") || !strcmp(nm, "next::can") || !strcmp(nm, "maybe::next::method") || !strncmp(nm, "NEXT::", 6)) {
    PN recv = potion_source(P, AST_EXPR, chain, PN_NIL, PN_NIL, 0, PN_NIL);
    PN args = PN_S(st, 1) != PN_NIL ? PN_S(st, 1) : potion_source(P, AST_LIST, PN_NIL, PN_NIL, PN_NIL, 0, PN_NIL);
    const char *kind = !strcmp(nm, "next::can") ? "can" : nm[0] == 'm' ? "maybe" : "method";
    const char *subn = !strncmp(nm, "NEXT::", 6) ? nm + 6 : p5_cursub; /* $self->NEXT::name: the named method, not the current sub */
    PN al = PN_TUP(p5_str_value(P, potion_str(P, p5_pkg[0] ? p5_pkg : "main")));
    PN call;
    al = PN_PUSH(al, p5_str_value(P, potion_str(P, subn)));
    al = PN_PUSH(al, p5_str_value(P, potion_str(P, kind)));
    al = PN_PUSH(al, recv);
    al = PN_PUSH(al, potion_source(P, AST_EXPR, PN_TUP(args), PN_NIL, PN_NIL, 0, PN_NIL));
    call = potion_source(P, AST_MSG, PN_STR("p5next"), potion_source(P, AST_LIST, al, PN_NIL, PN_NIL, 0, PN_NIL), PN_NIL, 0, PN_NIL);
    return PN_TUP(call);
  }
  if (!strncmp(nm, "SUPER::", 7)) {
    PN recv = potion_source(P, AST_EXPR, chain, PN_NIL, PN_NIL, 0, PN_NIL);
    PN args = PN_S(st, 1) != PN_NIL ? PN_S(st, 1) : potion_source(P, AST_LIST, PN_NIL, PN_NIL, PN_NIL, 0, PN_NIL);
    PN call = potion_source(P, AST_MSG, PN_STR("p5super"),
      potion_source(P, AST_LIST, PN_PUSH(PN_PUSH(PN_PUSH(PN_TUP(p5_str_value(P, potion_str(P, p5_pkg[0] ? p5_pkg : "main"))), recv),
                      p5_str_value(P, potion_str(P, nm + 7))),
                      potion_source(P, AST_EXPR, PN_TUP(args), PN_NIL, PN_NIL, 0, PN_NIL)), PN_NIL, PN_NIL, 0, PN_NIL),
      PN_NIL, 0, PN_NIL);
    return PN_TUP(call);
  }
  if (!strcmp(nm, "p5aref") || !strcmp(nm, "p5href") || !strcmp(nm, "p5lastidx") || !strcmp(nm, "p5sliceof"))
    return PN_PUSH(chain, st); /* postfix dereference: a plain message, not a method of the object */
  return PN_PUSH(chain, p5_meth_msg(P, st));
}
/* is E an element read: a hash/array element MSG or an elemchain (EXPR ending in at(KEY))? */
static PN p5_pat_elem_msg, p5_pat_elem_chain;
static int p5_is_elem(Potion *P, PN e) {
  PN caps[PN_AST_CAPS];
  if (!p5_pat_elem_msg) { /* MSG(@x | %x, args)  and  EXPR[ .. , MSG("at", args) ] */
    p5_pat_elem_msg = potion_source(P, AST_MSG, potion_ast_alt(P, PN_PUSH(PN_PUSH(PN_TUP0(),
      PN_STR("@*")), PN_STR("%*"))), potion_ast_some(P, -1), potion_ast_wild(P, -1), 0, PN_NIL);
    potion_gc_root(P, &p5_pat_elem_msg);
    p5_pat_elem_chain = potion_source(P, AST_EXPR, PN_PUSH(PN_PUSH(PN_TUP0(), potion_ast_lead(P, -1)),
      potion_source(P, AST_MSG, PN_STR("at"), potion_ast_wild(P, -1), potion_ast_wild(P, -1), 0, PN_NIL)), PN_NIL, PN_NIL, 0, PN_NIL);
    potion_gc_root(P, &p5_pat_elem_chain);
  }
  if (potion_ast_match(P, p5_pat_elem_msg, e, caps, 0)) return 1;
  return PN_IS_PTR(e) && PN_VTYPE(e) == PN_TSOURCE && PN_PART(e) == AST_EXPR && PN_IS_TUPLE(PN_S(e, 0)) &&
         PN_TUPLE_LEN(PN_S(e, 0)) >= 2 && potion_ast_match(P, p5_pat_elem_chain, e, caps, 0);
}
/* E = VALUE for an element chain read (EXPR [base, at(k)]): the last at(k) becomes put(k, VALUE) */
static PN p5_elem_store(Potion *P, long lineno, PN line, PN e, PN value) {
  PN items = PN_S(e, 0), out = PN_TUP0(), last, key;
  PN_SIZE i, n = PN_TUPLE_LEN(items);
  for (i = 0; i + 1 < n; i++) out = PN_PUSH(out, PN_TUPLE_AT(items, i));
  last = PN_TUPLE_AT(items, n - 1);
  key = PN_S(PN_S(last, 1), 0); /* the key tuple: copied, it belongs to the read chain */
  { PN kc = PN_TUP0();
    if (PN_IS_TUPLE(key)) for (i = 0; i < PN_TUPLE_LEN(key); i++) kc = PN_PUSH(kc, PN_TUPLE_AT(key, i));
    else kc = PN_TUP(key);
    out = PN_PUSH(out, potion_source(P, AST_MSG, PN_STR("put"),
            potion_source(P, AST_LIST, PN_PUSH(kc, value), PN_NIL, PN_NIL, lineno, line), PN_NIL, lineno, line)); }
  return potion_source(P, AST_EXPR, out, PN_NIL, PN_NIL, lineno, line);
}
/* E++ / E-- / ++E / --E on an element: E = E +/- 1 (a postfix yields the old value) */
static PN p5_elem_incdec(Potion *P, long lineno, PN line, PN e, int delta, int prefix) {
  PN one = potion_source(P, AST_VALUE, PN_NUM(1), PN_NIL, PN_NIL, lineno, line);
  PN val = potion_source(P, delta > 0 ? AST_PLUS : AST_MINUS, e, one, PN_NIL, lineno, line);
  PN st = PN_PART(e) == AST_MSG ? potion_source(P, AST_ASSIGN, e, val, PN_NIL, lineno, line) : p5_elem_store(P, lineno, line, e, val);
  /* the store has no useful value: (st && 0) || value, the value read after the store */
  {
    PN zero = potion_source(P, AST_VALUE, PN_ZERO, PN_NIL, PN_NIL, lineno, line);
    PN value = prefix ? e : potion_source(P, delta > 0 ? AST_MINUS : AST_PLUS, e, one, PN_NIL, lineno, line);
    return potion_source(P, AST_OR, potion_source(P, AST_AND, st, zero, PN_NIL, lineno, line), value, PN_NIL, lineno, line);
  }
}
/* use overload KEY => CODE, ...: the operator table of the current package */
static PN p5_use_overload(Potion *P, long lineno, PN line, PN items) {
  PN lst = potion_source(P, AST_LIST, items, PN_NIL, PN_NIL, lineno, line);
  return potion_source(P, AST_EXPR, PN_TUP(potion_source(P, AST_MSG, PN_STR("p5overload"),
           potion_source(P, AST_LIST, PN_PUSH(PN_TUP(p5_str_value(P, potion_str(P, p5_pkg[0] ? p5_pkg : "main"))),
                           potion_source(P, AST_EXPR, PN_TUP(lst), PN_NIL, PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line),
           PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line);
}
/* use mro 'c3': the current package resolves methods in C3 order */
static PN p5_use_mro(Potion *P, long lineno, PN line, PN items) {
  PN kind = PN_TUPLE_LEN(items) ? PN_TUPLE_AT(items, 0) : p5_str_value(P, PN_STR("dfs"));
  return potion_source(P, AST_EXPR, PN_TUP(potion_source(P, AST_MSG, PN_STR("p5mro"),
           potion_source(P, AST_LIST, PN_PUSH(PN_TUP(p5_str_value(P, potion_str(P, p5_pkg[0] ? p5_pkg : "main"))), kind), PN_NIL, PN_NIL, lineno, line),
           PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line);
}
/* use parent LIST / use base LIST: push the classes onto the current package's @ISA */
static PN p5_use_parent(Potion *P, long lineno, PN line, PN items) {
  PN lst = potion_source(P, AST_LIST, items, PN_NIL, PN_NIL, lineno, line);
  return potion_source(P, AST_EXPR, PN_TUP(potion_source(P, AST_MSG, PN_STR("p5isapush"),
           potion_source(P, AST_LIST, PN_PUSH(PN_TUP(p5_str_value(P, potion_str(P, p5_pkg[0] ? p5_pkg : "main"))),
                           potion_source(P, AST_EXPR, PN_TUP(lst), PN_NIL, PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line),
           PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line);
}
/* @ISA / @Pkg::ISA as an assignment target: the package's parents */
static PN p5_isa_pkg(Potion *P, PN name) {
  const char *n = PN_STR_PTR(name);
  long len = PN_STR_LEN(name);
  if (len == 4 && !strcmp(n, "@ISA")) return potion_str(P, p5_pkg[0] ? p5_pkg : "main");
  if (len > 6 && !strcmp(n + len - 5, "::ISA")) return potion_str2(P, (char *)n + 1, len - 6);
  return PN_NIL;
}
static PN p5_isa_assign(Potion *P, long lineno, PN line, PN lhs, PN rhs, PN fallback) {
  PN pkg;
  if (!(PN_IS_PTR(lhs) && PN_VTYPE(lhs) == PN_TSOURCE && PN_PART(lhs) == AST_MSG && PN_S(lhs, 1) == PN_NIL && PN_IS_STR(PN_S(lhs, 0))))
    return fallback;
  pkg = p5_isa_pkg(P, PN_S(lhs, 0));
  if (pkg == PN_NIL) return fallback;
  return potion_source(P, AST_EXPR, PN_TUP(potion_source(P, AST_MSG, PN_STR("p5setisa"),
           potion_source(P, AST_LIST, PN_PUSH(PN_TUP(p5_str_value(P, pkg)), potion_source(P, AST_EXPR, PN_TUP(rhs), PN_NIL, PN_NIL, lineno, line)),
                         PN_NIL, PN_NIL, lineno, line), PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line);
}
/* container variables (%h, @a) the parser has seen so far (declared, assigned as a whole, or
 * read). `$h{k} = v` / `$a[i] = v` on an unseen one is the first mention of a package variable:
 * the container is created first (perl autovivifies), else a nil local is written to. */
static char p5_seen_names[256][32];
static int p5_seen_n;
static int p5_seen(const char *name, long len) {
  int i;
  for (i = 0; i < p5_seen_n; i++)
    if (!strncmp(p5_seen_names[i], name, len) && p5_seen_names[i][len] == 0) return 1;
  return 0;
}
static void p5_see(const char *name, long len) {
  if (len < 32 && p5_seen_n < 256 && !p5_seen(name, len)) {
    memcpy(p5_seen_names[p5_seen_n], name, len);
    p5_seen_names[p5_seen_n++][len] = 0;
  }
}
static char p5_viv[64][32];
static int p5_viv_n, p5_viv_base[16], p5_viv_depth;
static void p5_viv_add(const char *name) { if (p5_viv_n < 64 && strlen(name) < 32) strcpy(p5_viv[p5_viv_n++], name); }
/* a file level `my @a;` / `my %h;` (no initializer, outside any block) is created when the program starts, like
 * the element-assignment vivification: a sub defined above it, called before it, then finds the container (perl keeps
 * what was stored there before the declaration ran). Returns 1 when the statement became a no-op. */
static int p5_top_decl(Potion *P, PN l) {
  PN nm = PN_S(l, 0);
  if (p5_local_depth != 0 || !PN_IS_STR(nm) || PN_STR_LEN(nm) >= 32 || p5_viv_depth <= 0 || p5_is_session(nm)) return 0;
  p5_viv_add(PN_STR_PTR(nm));
  p5_see(PN_STR_PTR(nm), PN_STR_LEN(nm));
  return 1;
}
/* a bare global scalar (`$x = 1;` at the top level of a file, `local $x`) is created when the program starts, like the
 * aggregates: a sub defined above it, and called later, then reads the same variable */
static int p5_viv_subs[16]; /* p5_sub_defs at the start of each file being parsed */
static void p5_viv_scalar(PN name) {
  const char *nm;
  if (!PN_IS_STR(name) || p5_viv_depth <= 0 || p5_viv_depth > 16 || p5_sub_defs == p5_viv_subs[p5_viv_depth - 1]) return; /* only when a sub of this file is already defined */
  nm = PN_STR_PTR(name);
  if (nm[0] != '$' || PN_STR_LEN(name) < 3 || PN_STR_LEN(name) >= 32 || strstr(nm, "::")) return;
  if (!((nm[1] >= 'a' && nm[1] <= 'z') || (nm[1] >= 'A' && nm[1] <= 'Z') || (nm[1] == '_' && nm[2] != 0))) return; /* $_ and the punctuation variables are lobby globals */
  if (!strcmp(nm, "$a") || !strcmp(nm, "$b") || p5_seen(nm, PN_STR_LEN(name))) return;
  p5_see(nm, PN_STR_LEN(name));
  p5_viv_add(nm);
}
static void p5_top_scalar(PN e) {
  PN lhs;
  if (!(PN_IS_PTR(e) && PN_VTYPE(e) == PN_TSOURCE && PN_PART(e) == AST_ASSIGN) || p5_local_depth != 0) return;
  lhs = PN_S(e, 0);
  if (PN_IS_PTR(lhs) && PN_VTYPE(lhs) == PN_TSOURCE && PN_PART(lhs) == AST_MSG && PN_S(lhs, 1) == PN_NIL) p5_viv_scalar(PN_S(lhs, 0));
}
static void p5_viv_begin(Potion *P) {
  p5_pkg_save(); p5_hh_save(P, 1); p5_pkg[0] = 0; /* a file starts in main */ if (p5_viv_depth < 16) { p5_viv_subs[p5_viv_depth] = p5_sub_defs; p5_viv_base[p5_viv_depth++] = p5_viv_n; } }
/* `%h = {}` / `@a = ()` statements for the containers first mentioned by an element assignment
 * in this parse, in front of the program */
static PN p5_viv_prologue(Potion *P, PN stmts) {
  PN pre = PN_TUP0();
  p5_pkg_restore(); p5_hh_restore(P);
  int i, base = p5_viv_depth > 0 ? p5_viv_base[--p5_viv_depth] : 0;
  long n = PN_IS_TUPLE(stmts) ? (long)PN_TUPLE_LEN(stmts) : 0;
  for (i = base; i < p5_viv_n; i++) {
    PN name = PN_STR(p5_viv[i]);
    PN lhs = potion_source(P, AST_MSG, name, PN_NIL, PN_NIL, 1, P->line);
    PN init = p5_viv[i][0] == '$' ? potion_source(P, AST_VALUE, PN_NIL, PN_NIL, PN_NIL, 1, P->line) : p5_viv[i][0] == '%'
      ? potion_source(P, AST_EXPR, PN_PUSH(PN_TUP(potion_source(P, AST_LIST, PN_TUP0(), PN_NIL, PN_NIL, 1, P->line)),
                        potion_source(P, AST_MSG, PN_STR("table"), PN_NIL, PN_NIL, 1, P->line)), PN_NIL, PN_NIL, 1, P->line)
      : potion_source(P, AST_EXPR, PN_TUP(potion_source(P, AST_LIST, PN_TUP0(), PN_NIL, PN_NIL, 1, P->line)), PN_NIL, PN_NIL, 1, P->line);
    pre = PN_PUSH(pre, potion_source(P, AST_ASSIGN, lhs, init, PN_NIL, 1, P->line));
  }
  p5_viv_n = base;
  if (p5_env_used) /* %ENV and %Config exist when the (maybe compiled) program starts */
    pre = PN_PUSH(pre, potion_source(P, AST_EXPR, PN_TUP(potion_source(P, AST_MSG, PN_STR("p5envinit"),
            potion_source(P, AST_LIST, PN_TUP0(), PN_NIL, PN_NIL, 1, P->line), PN_NIL, 1, P->line)), PN_NIL, PN_NIL, 1, P->line));
  if (PN_TUPLE_LEN(pre) == 0) return stmts;
  for (i = 0; i < n; i++) pre = PN_PUSH(pre, PN_TUPLE_AT(stmts, i));
  return pre;
}
static int p5_want_used; /* the source mentions wantarray */
static int p5_pos_used; /* the source mentions pos(): a statement `$x =~ m//g;` walks, else it is a plain match
                           (the pos table cannot see that the variable was assigned the same text again) */
/* is E a call whose context the caller decides: f(..), $o->m(..), $c->(..)? */
static int p5_is_callish(PN e) {
  while (PN_IS_PTR(e) && PN_VTYPE(e) == PN_TSOURCE && PN_PART(e) == AST_EXPR) {
    PN in = PN_S(e, 0);
    if (PN_IS_TUPLE(in) && PN_TUPLE_LEN(in) >= 1) e = PN_TUPLE_AT(in, PN_TUPLE_LEN(in) - 1);
    else if (PN_IS_PTR(in) && PN_VTYPE(in) == PN_TSOURCE) e = in;
    else break;
  }
  if (!(PN_IS_PTR(e) && PN_VTYPE(e) == PN_TSOURCE && PN_PART(e) == AST_MSG && PN_IS_STR(PN_S(e, 0)))) return 0;
  return !strcmp(PN_STR_PTR(PN_S(e, 0)), "p5meth") || !strcmp(PN_STR_PTR(PN_S(e, 0)), "p5callv") ||
         p5_is_subname(PN_STR_PTR(PN_S(e, 0)), PN_STR_LEN(PN_S(e, 0))) || p5_is_presub(PN_STR_PTR(PN_S(e, 0)), PN_STR_LEN(PN_S(e, 0)));
}
/* `$x = f(...)`: f runs in scalar context. wantarray reads the context from the global that p5want sets
 * (the call site; a call not wrapped is a list call), the callee takes it at entry (p5wantget) */
static PN p5_want_call(Potion *P, long lineno, PN line, int ctx) {
  return potion_source(P, AST_MSG, PN_STR(ctx < 0 ? "p5wantget" : "p5want"),
           potion_source(P, AST_LIST, ctx < 0 ? PN_TUP0() : PN_TUP(potion_source(P, AST_VALUE, PN_NUM(ctx), PN_NIL, PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line),
           PN_NIL, lineno, line);
}
static PN p5_want_wrap(Potion *P, long lineno, PN line, PN rhs) {
  /* p5wantend(p5want(0), RHS): the arguments run left to right, p5wantend resets the context and returns RHS */
  PN set = potion_source(P, AST_EXPR, PN_TUP(p5_want_call(P, lineno, line, 0)), PN_NIL, PN_NIL, lineno, line);
  PN call = potion_source(P, AST_MSG, PN_STR("p5wantend"),
              potion_source(P, AST_LIST, PN_PUSH(PN_TUP(set), rhs), PN_NIL, PN_NIL, lineno, line), PN_NIL, lineno, line);
  return potion_source(P, AST_EXPR, PN_TUP(call), PN_NIL, PN_NIL, lineno, line);
}
static PN p5_unparen(Potion *P, PN e);
static int p5_arrayish(Potion *P, PN item);
/* scalar(EXPR): the number of elements of a list or hash, a sub call runs in scalar context, else the value */
static PN p5_scalar_of(Potion *P, long lineno, PN line, PN e) {
  PN u = p5_unparen(P, e), nm = PN_NIL;
  {
    PN l = u;
    while (PN_IS_PTR(l) && PN_VTYPE(l) == PN_TSOURCE && PN_PART(l) == AST_EXPR && PN_IS_TUPLE(PN_S(l, 0)) && PN_TUPLE_LEN(PN_S(l, 0)) == 1) l = PN_TUPLE_AT(PN_S(l, 0), 0);
    if (PN_IS_PTR(l) && PN_VTYPE(l) == PN_TSOURCE && PN_PART(l) == AST_LIST && PN_IS_TUPLE(PN_S(l, 0)) && PN_TUPLE_LEN(PN_S(l, 0)) > 1)
      return p5_scalar_of(P, lineno, line, PN_TUPLE_AT(PN_S(l, 0), PN_TUPLE_LEN(PN_S(l, 0)) - 1)); /* scalar(42, &f): the comma operator, the last item */
  }
  {
    PN w = u;
    while (PN_IS_PTR(w) && PN_VTYPE(w) == PN_TSOURCE && PN_PART(w) == AST_EXPR && PN_IS_TUPLE(PN_S(w, 0)) && PN_TUPLE_LEN(PN_S(w, 0)) == 1) w = PN_TUPLE_AT(PN_S(w, 0), 0);
    if (PN_IS_PTR(w) && PN_VTYPE(w) == PN_TSOURCE && PN_PART(w) == AST_MSG && PN_IS_STR(PN_S(w, 0)) && PN_S(w, 1) == PN_NIL) nm = PN_S(w, 0);
  }
  if (p5_arrayish(P, e) || (nm != PN_NIL && PN_STR_LEN(nm) > 0 && PN_STR_PTR(nm)[0] == '%'))
    return potion_source(P, AST_EXPR, PN_PUSH(PN_TUPIF(e), potion_source(P, AST_MSG, PN_STR(nm != PN_NIL && PN_STR_LEN(nm) > 0 && PN_STR_PTR(nm)[0] == '@' ? "p5alen" : "length"), PN_NIL, PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line);
  if (p5_want_used && p5_is_callish(u)) return p5_want_wrap(P, lineno, line, u);
  return potion_source(P, AST_EXPR, PN_TUPIF(u), PN_NIL, PN_NIL, lineno, line);
}
static PN p5_want_void(Potion *P, long lineno, PN line, PN e) {
  PN set, call;
  if (!p5_want_used || !p5_is_callish(e)) return e;
  set = potion_source(P, AST_EXPR, PN_TUP(p5_want_call(P, lineno, line, 2)), PN_NIL, PN_NIL, lineno, line);
  call = potion_source(P, AST_MSG, PN_STR("p5wantend"),
           potion_source(P, AST_LIST, PN_PUSH(PN_TUP(set), e), PN_NIL, PN_NIL, lineno, line), PN_NIL, lineno, line);
  return potion_source(P, AST_EXPR, PN_TUP(call), PN_NIL, PN_NIL, lineno, line);
}
static PN p5_fix_assign(Potion *P, long lineno, PN line, PN n) {
  if (PN_IS_PTR(n) && PN_VTYPE(n) == PN_TSOURCE && PN_PART(n) == AST_ASSIGN) { /* ($x = $y) |= $z: $x = (($x = $y) | $z) */
    PN l = PN_S(n, 0);
    while (PN_IS_PTR(l) && PN_VTYPE(l) == PN_TSOURCE && (PN_PART(l) == AST_EXPR || PN_PART(l) == AST_LIST) && PN_IS_TUPLE(PN_S(l, 0)) && PN_TUPLE_LEN(PN_S(l, 0)) == 1) l = PN_TUPLE_AT(PN_S(l, 0), 0);
    if (PN_IS_PTR(l) && PN_VTYPE(l) == PN_TSOURCE && PN_PART(l) == AST_ASSIGN) { PN_SRC(n)->a[0] = PN_SRC(PN_S(l, 0)); PN_TOUCH(n); }
  }
  if (PN_IS_PTR(n) && PN_VTYPE(n) == PN_TSOURCE && PN_PART(n) == AST_ASSIGN) { /* $#{$r} = N, $#a = N inside an expression: resize */
    PN l = PN_S(n, 0), ex;
    while (PN_IS_PTR(l) && PN_VTYPE(l) == PN_TSOURCE && PN_PART(l) == AST_EXPR && PN_IS_TUPLE(PN_S(l, 0)) && PN_TUPLE_LEN(PN_S(l, 0)) == 1) l = PN_TUPLE_AT(PN_S(l, 0), 0);
    ex = PN_IS_PTR(l) && PN_VTYPE(l) == PN_TSOURCE && PN_PART(l) == AST_MINUS ? (PN)PN_SRC(l)->a[0] : PN_NIL;
    if (PN_IS_PTR(ex) && PN_VTYPE(ex) == PN_TSOURCE && PN_PART(ex) == AST_EXPR && PN_IS_TUPLE(PN_S(ex, 0)) && PN_TUPLE_LEN(PN_S(ex, 0)) >= 2) {
      PN items = PN_S(ex, 0), last = PN_TUPLE_AT(items, PN_TUPLE_LEN(items) - 1);
      if (PN_IS_PTR(last) && PN_VTYPE(last) == PN_TSOURCE && PN_PART(last) == AST_MSG && PN_IS_STR(PN_S(last, 0)) && !strcmp(PN_STR_PTR(PN_S(last, 0)), "length")) {
        PN pre = PN_TUP0(); PN_SIZE i;
        for (i = 0; i + 1 < PN_TUPLE_LEN(items); i++) pre = PN_PUSH(pre, PN_TUPLE_AT(items, i));
        return potion_source(P, AST_EXPR, PN_PUSH(pre, potion_source(P, AST_MSG, PN_STR("p5setlast"),
                 potion_source(P, AST_LIST, PN_TUP(potion_source(P, AST_PLUS, p5_unparen(P, PN_S(n, 1)), potion_source(P, AST_VALUE, PN_NUM(1), PN_NIL, PN_NIL, lineno, line), PN_NIL, lineno, line)),
                               PN_NIL, PN_NIL, lineno, line), PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line);
      }
    }
  }
  if (p5_tied_n && PN_IS_PTR(n) && PN_VTYPE(n) == PN_TSOURCE && PN_PART(n) == AST_ASSIGN) { /* $tied[i] = v, $tied{k} = v: put */
    PN l = PN_S(n, 0);
    if (PN_IS_PTR(l) && PN_VTYPE(l) == PN_TSOURCE && PN_PART(l) == AST_EXPR && PN_IS_TUPLE(PN_S(l, 0)) && PN_TUPLE_LEN(PN_S(l, 0)) == 2) {
      PN base = PN_TUPLE_AT(PN_S(l, 0), 0), at = PN_TUPLE_AT(PN_S(l, 0), 1);
      if (PN_IS_PTR(base) && PN_VTYPE(base) == PN_TSOURCE && PN_PART(base) == AST_MSG && PN_IS_STR(PN_S(base, 0)) && p5_tied_name(PN_S(base, 0)) &&
          PN_PART(at) == AST_MSG && PN_IS_STR(PN_S(at, 0)) && !strcmp(PN_STR_PTR(PN_S(at, 0)), "at") && PN_IS_PTR(PN_S(at, 1))) {
        PN key = PN_S(PN_S(at, 1), 0);
        PN args = PN_PUSH(PN_IS_TUPLE(key) ? key : PN_TUP(key), PN_S(n, 1));
        return potion_source(P, AST_EXPR, PN_PUSH(PN_TUP(base), potion_source(P, AST_MSG, PN_STR("put"),
                 potion_source(P, AST_LIST, args, PN_NIL, PN_NIL, lineno, line), PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line);
      }
    }
  }
  if (p5_tied_n && PN_IS_PTR(n) && PN_VTYPE(n) == PN_TSOURCE && PN_PART(n) == AST_ASSIGN) { /* $tied = v: STORE */
    PN vm = p5_var_msg(PN_S(n, 0));
    if (vm != PN_NIL && PN_STR_PTR(PN_S(vm, 0))[0] == '$' && p5_tied_name(PN_S(vm, 0))) {
      PN lhs = potion_source(P, AST_MSG, PN_S(vm, 0), PN_NIL, PN_NIL, lineno, line);
      PN rhs = p5_fcall(P, lineno, line, "p5tstore", PN_PUSH(PN_TUP(lhs), PN_S(n, 1)));
      PN_SRC(n)->a[0] = PN_SRC(lhs); PN_SRC(n)->a[1] = PN_SRC(rhs); PN_TOUCH(n);
      return n;
    }
  }
  if (PN_IS_PTR(n) && PN_VTYPE(n) == PN_TSOURCE && PN_PART(n) == AST_ASSIGN) { /* @b = @a copies */
    PN c = p5_copy_rhs(P, PN_S(n, 0), PN_S(n, 1));
    if (p5_is_arrvar(PN_S(n, 0)) && p5_readline_all(P, PN_S(n, 1)) != PN_NIL) c = p5_readline_all(P, PN_S(n, 1)); /* @a = <FH> */
    if (c != PN_S(n, 1)) { PN_SRC(n)->a[1] = PN_SRC(c); PN_TOUCH(n); }
  }
  if (PN_IS_PTR(n) && PN_VTYPE(n) == PN_TSOURCE && PN_PART(n) == AST_ASSIGN) { /* $x = splice(..): the last removed element */
    PN lhs = PN_S(n, 0), rhs = PN_S(n, 1), last = rhs;
    while (PN_IS_PTR(last) && PN_VTYPE(last) == PN_TSOURCE && PN_PART(last) == AST_EXPR && PN_IS_TUPLE(PN_S(last, 0)) && PN_TUPLE_LEN(PN_S(last, 0)) >= 1)
      last = PN_TUPLE_AT(PN_S(last, 0), PN_TUPLE_LEN(PN_S(last, 0)) - 1);
    if (PN_IS_PTR(lhs) && PN_VTYPE(lhs) == PN_TSOURCE && PN_PART(lhs) == AST_MSG && PN_S(lhs, 1) == PN_NIL && PN_IS_STR(PN_S(lhs, 0)) &&
        PN_STR_LEN(PN_S(lhs, 0)) > 1 && PN_STR_PTR(PN_S(lhs, 0))[0] == '$' &&
        PN_IS_PTR(last) && PN_VTYPE(last) == PN_TSOURCE && PN_PART(last) == AST_MSG && PN_IS_STR(PN_S(last, 0)) && !strcmp(PN_STR_PTR(PN_S(last, 0)), "p5splice") &&
        PN_IS_PTR(rhs) && PN_PART(rhs) == AST_EXPR) {
      PN items = PN_IS_TUPLE(PN_S(rhs, 0)) ? PN_S(rhs, 0) : PN_TUP(PN_S(rhs, 0));
      PN_SRC(n)->a[1] = PN_SRC(potion_source(P, AST_EXPR, PN_PUSH(items, potion_source(P, AST_MSG, PN_STR("p5lastel"), PN_NIL, PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line)); PN_TOUCH(n);
    }
  }
  if (PN_IS_PTR(n) && PN_VTYPE(n) == PN_TSOURCE && PN_PART(n) == AST_ASSIGN) { /* @a = f(): a scalar result is a one element list */
    PN lhs = PN_S(n, 0);
    if (PN_IS_PTR(lhs) && PN_VTYPE(lhs) == PN_TSOURCE && PN_PART(lhs) == AST_MSG && PN_S(lhs, 1) == PN_NIL && PN_IS_STR(PN_S(lhs, 0)) &&
        PN_STR_LEN(PN_S(lhs, 0)) > 1 && PN_STR_PTR(PN_S(lhs, 0))[0] == '@' && p5_is_callish(PN_S(n, 1))) {
      PN call = potion_source(P, AST_MSG, PN_STR("p5aslist"),
                  potion_source(P, AST_LIST, PN_TUP(PN_S(n, 1)), PN_NIL, PN_NIL, lineno, line), PN_NIL, lineno, line);
      PN_SRC(n)->a[1] = PN_SRC(potion_source(P, AST_EXPR, PN_TUP(call), PN_NIL, PN_NIL, lineno, line)); PN_TOUCH(n);
    }
  }
  if (p5_want_used && PN_IS_PTR(n) && PN_VTYPE(n) == PN_TSOURCE && PN_PART(n) == AST_ASSIGN) {
    PN lhs = PN_S(n, 0);
    if (PN_IS_PTR(lhs) && PN_VTYPE(lhs) == PN_TSOURCE && PN_PART(lhs) == AST_MSG && PN_S(lhs, 1) == PN_NIL && PN_IS_STR(PN_S(lhs, 0)) &&
        PN_STR_LEN(PN_S(lhs, 0)) > 1 && PN_STR_PTR(PN_S(lhs, 0))[0] == '$' && p5_is_callish(PN_S(n, 1))) {
      PN_SRC(n)->a[1] = PN_SRC(p5_want_wrap(P, lineno, line, PN_S(n, 1))); PN_TOUCH(n);
    }
  }
  if (PN_IS_PTR(n) && PN_VTYPE(n) == PN_TSOURCE && PN_PART(n) == AST_ASSIGN && p5_is_elem(P, PN_S(n, 0)) && PN_PART(PN_S(n, 0)) == AST_EXPR)
    return p5_elem_store(P, lineno, line, PN_S(n, 0), PN_S(n, 1)); /* $r->{k} op= v */
  if (PN_IS_PTR(n) && PN_VTYPE(n) == PN_TSOURCE && PN_PART(n) == AST_ASSIGN)
    n = p5_isa_assign(P, lineno, line, PN_S(n, 0), PN_S(n, 1), n);
  if (PN_IS_PTR(n) && PN_VTYPE(n) == PN_TSOURCE && PN_PART(n) == AST_ASSIGN) {
    PN lhs = PN_S(n, 0);
    while (PN_IS_PTR(lhs) && PN_VTYPE(lhs) == PN_TSOURCE && PN_PART(lhs) == AST_EXPR && PN_IS_TUPLE(PN_S(lhs, 0)) && PN_TUPLE_LEN(PN_S(lhs, 0)) == 1)
      lhs = PN_TUPLE_AT(PN_S(lhs, 0), 0); /* $a[1] op= v: the element read is wrapped */
    if (PN_IS_PTR(lhs) && PN_VTYPE(lhs) == PN_TSOURCE && PN_PART(lhs) == AST_MSG && PN_S(lhs, 1) != PN_NIL &&
        PN_IS_STR(PN_S(lhs, 0)) && PN_STR_LEN(PN_S(lhs, 0)) > 1 &&
        (PN_STR_PTR(PN_S(lhs, 0))[0] == '%' || PN_STR_PTR(PN_S(lhs, 0))[0] == '@') &&
        !p5_seen(PN_STR_PTR(PN_S(lhs, 0)), PN_STR_LEN(PN_S(lhs, 0)))) {
      PN name = PN_S(lhs, 0);
      p5_see(PN_STR_PTR(name), PN_STR_LEN(name));
      if (p5_viv_n < 64 && PN_STR_LEN(name) < 32) { /* created at the start of the program */
        memcpy(p5_viv[p5_viv_n], PN_STR_PTR(name), PN_STR_LEN(name));
        p5_viv[p5_viv_n++][PN_STR_LEN(name)] = 0;
      }
    }
  }
  if (PN_IS_PTR(n) && PN_VTYPE(n) == PN_TSOURCE && PN_PART(n) == AST_ASSIGN) {
    PN r = p5_deref_target(PN_S(n, 0));
    if (r != PN_NIL) { /* $$r = EXPR => $r.p5assign(EXPR) */
      PN args = potion_source(P, AST_LIST, PN_TUP(PN_S(n, 1)), PN_NIL, PN_NIL, lineno, line);
      return potion_source(P, AST_EXPR,
               PN_PUSH(PN_TUP(r), potion_source(P, AST_MSG, PN_STR("p5assign"), args, PN_NIL, lineno, line)),
               PN_NIL, PN_NIL, lineno, line);
    }
  }
  if (PN_IS_PTR(n) && PN_VTYPE(n) == PN_TSOURCE && PN_PART(n) == AST_ASSIGN &&
      p5_is_hints_read(P, PN_S(n, 0)))
    return p5_hints_call(P, lineno, line, PN_S(n, 1));
  return n;
}
static PN p5_local_restores(Potion *P, long lineno, PN line, PN out, PN names, PN tmps, int n) {
  int i;
  for (i = 0; i < n; i++)
    if (!PN_IS_STR(PN_TUPLE_AT(names, i))) { /* an element: CONTAINER.p5lrestore(key, saved) */
      PN el = PN_TUPLE_AT(names, i), key = PN_TUPLE_AT(PN_S(PN_S(el, 1), 0), 0);
      PN args = potion_source(P, AST_LIST, PN_PUSH(PN_TUP(key),
                  potion_source(P, AST_MSG, PN_TUPLE_AT(tmps, i), PN_NIL, PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line);
      out = PN_PUSH(out, potion_source(P, AST_EXPR, PN_PUSH(PN_TUP(potion_source(P, AST_MSG, PN_S(el, 0), PN_NIL, PN_NIL, lineno, line)),
              potion_source(P, AST_MSG, PN_STR("p5lrestore"), args, PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line));
    } else
    if (p5_is_hint(PN_TUPLE_AT(names, i)))
      out = PN_PUSH(out, p5_hints_call(P, lineno, line,
              potion_source(P, AST_MSG, PN_TUPLE_AT(tmps, i), PN_NIL, PN_NIL, lineno, line)));
    else
    out = PN_PUSH(out, potion_source(P, AST_ASSIGN,
            potion_source(P, AST_MSG, PN_TUPLE_AT(names, i), PN_NIL, PN_NIL, lineno, line),
            potion_source(P, AST_MSG, PN_TUPLE_AT(tmps, i), PN_NIL, PN_NIL, lineno, line),
            PN_NIL, lineno, line));
  return out;
}
/* a statement that has a value (an expression, operators included), not a block/proto/lick */
static int p5_is_value_ast(PN st) {
  int part;
  if (!PN_IS_PTR(st) || PN_VTYPE(st) != PN_TSOURCE) return 0;
  part = PN_PART(st);
  return part != AST_BLOCK && part != AST_PROTO && part != AST_LICK && part != AST_DEBUG && part != AST_CODE;
}
static PN p5_local_blockend(Potion *P, long lineno, PN line, PN stmts) {
  int mark, i, nloc;
  long n, j;
  PN out, tmps = PN_TUP0(), names = PN_TUP0(), lv, lvm;
  char nb[40];
  int nn;
  p5_local_depth--;
  p5_pkg_restore(); p5_hh_restore(P);
  if (p5_local_depth >= 0 && p5_local_depth < 16) p5_state_n = p5_state_marks[p5_local_depth];
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
    if (!PN_IS_STR(name)) { /* an element: tmp = CONTAINER.p5lsave(key) */
      PN key = PN_TUPLE_AT(PN_S(PN_S(name, 1), 0), 0);
      out = PN_PUSH(out, potion_source(P, AST_ASSIGN, potion_source(P, AST_MSG, tmp, PN_NIL, PN_NIL, lineno, line),
              potion_source(P, AST_EXPR, PN_PUSH(PN_TUP(potion_source(P, AST_MSG, PN_S(name, 0), PN_NIL, PN_NIL, lineno, line)),
                potion_source(P, AST_MSG, PN_STR("p5lsave"),
                  potion_source(P, AST_LIST, PN_TUP(key), PN_NIL, PN_NIL, lineno, line), PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line),
              PN_NIL, lineno, line));
      continue;
    }
    out = PN_PUSH(out, potion_source(P, AST_ASSIGN,
            potion_source(P, AST_MSG, tmp, PN_NIL, PN_NIL, lineno, line),
            p5_is_hint(name)
              ? p5_hints_read(P, lineno, line)
              : (PN_STR_PTR(name)[0] == '@' || PN_STR_PTR(name)[0] == '%')
                /* a list or hash is assigned in place: save a copy */
                ? potion_source(P, AST_EXPR, PN_PUSH(PN_TUP(potion_source(P, AST_MSG, name, PN_NIL, PN_NIL, lineno, line)),
                    potion_source(P, AST_MSG, PN_STR("clone"), PN_NIL, PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line)
                : potion_source(P, AST_MSG, name, PN_NIL, PN_NIL, lineno, line),
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
    } else if (j == n - 1 && p5_is_value_ast(st)) {
      out = PN_PUSH(out, potion_source(P, AST_ASSIGN, lvm, st, PN_NIL, lineno, line));
    } else {
      out = PN_PUSH(out, st);
    }
  }
  out = p5_local_restores(P, lineno, line, out, names, tmps, nloc);
  if (n > 0 && p5_is_value_ast(PN_TUPLE_AT(stmts, n - 1)) && p5_return_arg(PN_TUPLE_AT(stmts, n - 1)) == PN_NIL)
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
static PN p5_pat_subdef;
static int p5_is_subdef(Potion *P, PN st) {
  PN caps[PN_AST_CAPS];
  if (!p5_pat_subdef) { /* ASSIGN(?, EXPR[ sub {..} | p5defsub(..) ]): a sub of a package is Foo::name = p5defsub("Foo::name", sub {..}) */
    p5_pat_subdef = potion_source(P, AST_ASSIGN, potion_ast_wild(P, -1), potion_source(P, AST_EXPR, PN_TUP(
      potion_ast_alt(P, PN_PUSH(PN_PUSH(PN_TUP0(),
        potion_source(P, AST_PROTO, potion_ast_wild(P, -1), potion_ast_wild(P, -1), potion_ast_wild(P, -1), 0, PN_NIL)),
        potion_source(P, AST_MSG, PN_STR("p5defsub"), potion_ast_wild(P, -1), potion_ast_wild(P, -1), 0, PN_NIL)))),
      PN_NIL, PN_NIL, 0, PN_NIL), PN_NIL, 0, PN_NIL);
    potion_gc_root(P, &p5_pat_subdef);
  }
  return potion_ast_match(P, p5_pat_subdef, st, caps, 0) && p5_assign_name(st) != PN_NIL;
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
static int p5_has_forward_ref(Potion *P, PN stmts) {
  long i, j;
  for (i = 0; i < (long)PN_TUPLE_LEN(stmts); i++) {
    PN st = PN_TUPLE_AT(stmts, i);
    if (!p5_is_subdef(P, st)) continue;
    for (j = 0; j < i; j++) {
      int m = 0, a = 0;
      p5_scan(PN_TUPLE_AT(stmts, j), p5_assign_name(st), &m, &a);
      if (m && !a) return 1;
    }
  }
  return 0;
}
/* names assigned by the statements of a block, recursing into nested blocks */
static int p5_block_names(PN blk, PN *names, int nn, int max) {
  PN sts = PN_S(blk, 0);
  long k;
  if (!PN_IS_TUPLE(sts)) return nn;
  for (k = 0; k < (long)PN_TUPLE_LEN(sts) && nn < max; k++) {
    PN it = PN_TUPLE_AT(sts, k), nm;
    if (PN_IS_PTR(it) && PN_VTYPE(it) == PN_TSOURCE && PN_PART(it) == AST_BLOCK)
      nn = p5_block_names(it, names, nn, max);
    else if ((nm = p5_assign_name(it)) != PN_NIL)
      names[nn++] = nm;
  }
  return nn;
}
static int p5_parse_depth;
static PN p5_predeclare(Potion *P, PN stmts) {
  PN pre = PN_TUP0(), subs = PN_TUP0(), rest = PN_TUP0(), seen = PN_TUP0();
  long i, j, k;
  /* not for a required file: its subs are spliced into a BEGIN block, and
   * hoisting there breaks compiled (.tc) programs (not root-caused) */
  if (p5_parse_depth > 1 || !PN_IS_TUPLE(stmts) || !p5_has_forward_ref(P, stmts)) return stmts;
  for (i = 0; i < (long)PN_TUPLE_LEN(stmts); i++) {
    PN st = PN_TUPLE_AT(stmts, i), names[64];
    int nn = 0;
    if (p5_is_subdef(P, st)) { subs = PN_PUSH(subs, st); }
    else rest = PN_PUSH(rest, st);
    if (PN_IS_PTR(st) && PN_PART(st) == AST_EXPR && PN_IS_TUPLE(PN_S(st, 0))) { /* my ($a,$b) = ... */
      for (k = 0; k < (long)PN_TUPLE_LEN(PN_S(st, 0)) && nn < 64; k++) {
        PN nm = p5_assign_name(PN_TUPLE_AT(PN_S(st, 0), k));
        if (nm != PN_NIL) names[nn++] = nm;
      }
    } else if (PN_IS_PTR(st) && PN_VTYPE(st) == PN_TSOURCE && PN_PART(st) == AST_BLOCK) {
      /* BEGIN { require 'file' }: the file's subs/variables are assigned in (nested) blocks */
      nn = p5_block_names(st, names, nn, 64);
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
static PN p5_unparen(Potion *P, PN e);
static int p5_arrayish(Potion *P, PN item);
/* f((a == b), c): a parenthesized single value is the value (a list, (@a), is spread later). The
 * argument list may itself arrive as one parenthesized list item. */
static PN p5_pat_listlit;
static int p5_is_listlit(Potion *P, PN it) {
  PN caps[PN_AST_CAPS], list;
  if (!p5_pat_listlit) { /* LIST, or EXPR[LIST] */
    list = potion_source(P, AST_LIST, potion_ast_wild(P, -1), potion_ast_wild(P, -1), potion_ast_wild(P, -1), 0, PN_NIL);
    p5_pat_listlit = potion_ast_alt(P, PN_PUSH(PN_PUSH(PN_TUP0(), list),
      potion_source(P, AST_EXPR, PN_TUP(list), PN_NIL, PN_NIL, 0, PN_NIL)));
    potion_gc_root(P, &p5_pat_listlit);
  }
  return potion_ast_match(P, p5_pat_listlit, it, caps, 0);
}
static PN p5_unparen_args(Potion *P, PN items) {
  PN un = PN_TUP0();
  PN_SIZE k, m;
  if (!PN_IS_TUPLE(items)) return items;
  for (k = 0; k < PN_TUPLE_LEN(items); k++) {
    PN it = PN_TUPLE_AT(items, k), lst = it;
    if (PN_IS_PTR(it) && PN_VTYPE(it) == PN_TSOURCE && PN_PART(it) == AST_EXPR && PN_IS_TUPLE(PN_S(it, 0)) && PN_TUPLE_LEN(PN_S(it, 0)) == 1)
      lst = PN_TUPLE_AT(PN_S(it, 0), 0);
    if (PN_IS_PTR(lst) && PN_VTYPE(lst) == PN_TSOURCE && PN_PART(lst) == AST_LIST && PN_S(lst, 0) == PN_NIL) continue; /* () */
    if (PN_IS_PTR(lst) && PN_VTYPE(lst) == PN_TSOURCE && PN_PART(lst) == AST_LIST && PN_IS_TUPLE(PN_S(lst, 0))) {
      /* a literal list (a, b) / (x) / (): its items are the arguments themselves */
      PN inner = p5_unparen_args(P, PN_S(lst, 0));
      if (PN_TUPLE_LEN(PN_S(lst, 0)) == 1 && p5_arrayish(P, PN_TUPLE_AT(PN_S(lst, 0), 0)) && !p5_is_listlit(P, PN_TUPLE_AT(PN_S(lst, 0), 0))) { un = PN_PUSH(un, PN_TUPLE_AT(PN_S(lst, 0), 0)); continue; }
      for (m = 0; m < PN_TUPLE_LEN(inner); m++) un = PN_PUSH(un, PN_TUPLE_AT(inner, m));
      continue;
    }
    un = PN_PUSH(un, p5_listctx(P, it)); /* an argument is in list context: a //g match yields all matches */
  }
  return un;
}
static PN p5_flatten_multi(Potion *P, long lineno, PN line, PN list_ast);
/* f(@a, 1) for a sub of ours: the array items are spread, so the call is f.p5callv(flat list) */
static PN p5_spread_call(Potion *P, long lineno, PN line, PN m, PN items) {
  PN_SIZE k;
  int any = 0;
  if (!PN_IS_TUPLE(items) || !PN_IS_STR(PN_S(m, 0)) || PN_S(m, 1) != PN_NIL) return PN_NIL;
  if (!p5_is_subname(PN_STR_PTR(PN_S(m, 0)), PN_STR_LEN(PN_S(m, 0))) && !p5_is_presub(PN_STR_PTR(PN_S(m, 0)), PN_STR_LEN(PN_S(m, 0)))) return PN_NIL;
  for (k = 0; k < PN_TUPLE_LEN(items); k++) if (p5_arrayish(P, PN_TUPLE_AT(items, k)) && !p5_is_listlit(P, PN_TUPLE_AT(items, k))) any = 1;
  if (!any && PN_TUPLE_LEN(items) <= 12) return PN_NIL; /* more than P5_NARGS arguments: p5callv packs the rest */
  return PN_PUSH(PN_TUP(potion_source(P, AST_MSG, PN_S(m, 0), PN_NIL, PN_NIL, lineno, line)),
           potion_source(P, AST_MSG, PN_STR("p5callv"),
             potion_source(P, AST_LIST, PN_TUP(potion_source(P, AST_EXPR, PN_TUP(PN_TUPLE_LEN(items) == 1 ? PN_TUPLE_AT(items, 0) : p5_flatten_multi(P, lineno, line,
               potion_source(P, AST_LIST, items, PN_NIL, PN_NIL, lineno, line))), PN_NIL, PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line),
             PN_NIL, lineno, line));
}
static PN p5_pad_args(Potion *P, PN name, PN items) {
  static const struct { const char *n; int max; } pads[] = {
    {"substr", 3}, {"unlink", 6}, {"umask", 1}, {"mkdir", 2}, {"chmod", 7}, {"chown", 8}, {"utime", 8}, {"index", 3}, {"rindex", 3}, {"sprintf", 7}, {"split", 3}, {"mro::get_linear_isa", 2}, {0, 0}
  };
  int i;
  PN nm = PN_S(name, 0);
  if (!PN_IS_STR(nm) || !PN_IS_TUPLE(items)) return items;
  items = p5_unparen_args(P, items);
  if (PN_STR_LEN(nm) == 7 && !memcmp(PN_STR_PTR(nm), "sprintf", 7) && PN_TUPLE_LEN(items) >= 2) {
    /* sprintf FMT, @list: the arguments after the format become one tuple, spread by the native */
    int any = 0; long k;
    for (k = 1; k < (long)PN_TUPLE_LEN(items); k++) if (p5_arrayish(P, PN_TUPLE_AT(items, k)) && !p5_is_listlit(P, PN_TUPLE_AT(items, k))) any = 1;
    if (any) {
      PN rest = PN_TUP0();
      for (k = 1; k < (long)PN_TUPLE_LEN(items); k++) rest = PN_PUSH(rest, PN_TUPLE_AT(items, k));
      items = PN_PUSH(PN_TUP(PN_TUPLE_AT(items, 0)), potion_source(P, AST_EXPR, PN_TUP(PN_TUPLE_LEN(rest) == 1 ? PN_TUPLE_AT(rest, 0) : p5_flatten_multi(P, 0, PN_NIL, potion_source(P, AST_LIST, rest, PN_NIL, PN_NIL, 0, PN_NIL))), PN_NIL, PN_NIL, 0, PN_NIL));
    }
  }
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
/* a bare block is a loop that runs once for last / next: with one of them inside it becomes
 * `$once = 1; while ($once) { $once = 0; BODY }` */
static void p5_loopctl_seen(Potion *P, PN node, void *ud) {
  if (PN_PART(node) == AST_MSG && PN_IS_STR(PN_S(node, 0)) && (PN_S(node, 0) == PN_break || PN_S(node, 0) == PN_continue)) *(int *)ud = 1;
  /* a skip(..) call may leave a SKIP: block (p5_skip_walk adds the `last SKIP`), so the block needs the loop */
  if (PN_PART(node) == AST_MSG && PN_IS_STR(PN_S(node, 0)) && PN_S(node, 1) != PN_NIL && !strcmp(PN_STR_PTR(PN_S(node, 0)), "skip")) *(int *)ud = 1;
}
static PN p5_bare_block(Potion *P, long lineno, PN line, PN b) {
  static int ctr = 0;
  int seen = 0;
  char nb[40];
  int nn;
  PN once, one, zero, assign0, whilemsg;
  potion_ast_walk(P, b, PN_AST_NOPROTO, p5_loopctl_seen, &seen);
  if (!seen) return b;
  nn = snprintf(nb, sizeof nb, "$__bb_%d", ctr++);
  once = potion_source(P, AST_MSG, PN_STRN(nb, nn), PN_NIL, PN_NIL, lineno, line);
  one = potion_source(P, AST_VALUE, PN_NUM(1), PN_NIL, PN_NIL, lineno, line);
  zero = potion_source(P, AST_VALUE, PN_ZERO, PN_NIL, PN_NIL, lineno, line);
  assign0 = potion_source(P, AST_EXPR, PN_TUP(potion_source(P, AST_ASSIGN, once, zero, PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line);
  b = p5_block_cat(P, lineno, line, potion_source(P, AST_BLOCK, PN_TUP(assign0), PN_NIL, PN_NIL, lineno, line), b);
  whilemsg = potion_source(P, AST_MSG, PN_while, potion_source(P, AST_LIST, PN_TUP(once), PN_NIL, PN_NIL, lineno, line), b, lineno, line);
  return potion_source(P, AST_BLOCK, PN_PUSH(PN_TUP(potion_source(P, AST_EXPR, PN_TUP(potion_source(P, AST_ASSIGN, once, one, PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line)),
                                            potion_source(P, AST_EXPR, PN_TUP(whilemsg), PN_NIL, PN_NIL, lineno, line)),
                       PN_NIL, PN_NIL, lineno, line);
}
/* the statements of block B followed by those of block C, as one block */
static PN p5_block_cat(Potion *P, long lineno, PN line, PN b, PN c) {
  PN st = PN_TUP0();
  if (PN_IS_TUPLE(PN_S(b, 0))) PN_TUPLE_EACH(PN_S(b, 0), i, v, { st = PN_PUSH(st, v); });
  if (PN_IS_TUPLE(PN_S(c, 0))) PN_TUPLE_EACH(PN_S(c, 0), i, v, { st = PN_PUSH(st, v); });
  return potion_source(P, AST_BLOCK, st, PN_NIL, PN_NIL, lineno, line);
}
static PN p5_cfor(Potion *P, long lineno, PN line, PN init, PN cond, PN step, PN body);
/* while (COND) BODY continue CONT: CONT runs at the loop test (it also runs after `next`) */
static PN p5_while_cont(Potion *P, long lineno, PN line, PN cl, PN body, PN cont, int until) {
  PN cond = PN_IS_TUPLE(PN_S(cl, 0)) && PN_TUPLE_LEN(PN_S(cl, 0)) ? PN_TUPLE_AT(PN_S(cl, 0), 0) : PN_NIL;
  PN step = cont; /* inline: a `return` in the continue block leaves the sub */
  cond = p5_cond(P, lineno, line, cond);
  if (until) cond = potion_source(P, AST_NOT, cond, PN_NIL, PN_NIL, lineno, line);
  return p5_cfor(P, lineno, line, PN_NIL, cond, step, body);
}
/* { BODY } continue { CB }: `$once = 1; while ($once) { $once = 0; BODY } continue { CB }` */
static PN p5_bare_cont(Potion *P, long lineno, PN line, PN b, PN cb) {
  static int ctr = 0;
  char nb[40];
  int nn = snprintf(nb, sizeof nb, "$__bc_%d", ctr++);
  PN once = potion_source(P, AST_MSG, PN_STRN(nb, nn), PN_NIL, PN_NIL, lineno, line);
  PN one = potion_source(P, AST_VALUE, PN_NUM(1), PN_NIL, PN_NIL, lineno, line);
  PN zero = potion_source(P, AST_VALUE, PN_ZERO, PN_NIL, PN_NIL, lineno, line);
  PN assign1 = potion_source(P, AST_EXPR, PN_TUP(potion_source(P, AST_ASSIGN, once, one, PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line);
  PN assign0 = potion_source(P, AST_EXPR, PN_TUP(potion_source(P, AST_ASSIGN, once, zero, PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line);
  PN cl = potion_source(P, AST_LIST, PN_TUP(once), PN_NIL, PN_NIL, lineno, line);
  PN body = p5_block_cat(P, lineno, line, potion_source(P, AST_BLOCK, PN_TUP(assign0), PN_NIL, PN_NIL, lineno, line), b);
  PN loop = p5_while_cont(P, lineno, line, cl, body, cb, 0);
  return potion_source(P, AST_BLOCK, PN_PUSH(PN_TUP(assign1), loop), PN_NIL, PN_NIL, lineno, line);
}
static PN p5_cfor(Potion *P, long lineno, PN line, PN init, PN cond, PN step, PN body) {
  static int ctr = 0;
  PN stmts = PN_TUP0(), newstmts = PN_TUP0();
  if (cond == PN_NIL)
    cond = potion_source(P, AST_VALUE, PN_NUM(1), PN_NIL, PN_NIL, lineno, line);
  if (PN_IS_TUPLE(PN_S(body, 0)))
    PN_TUPLE_EACH(PN_S(body, 0), i, v, { newstmts = PN_PUSH(newstmts, v); });
#define P5_V(part, a1) potion_source(P, part, a1, PN_NIL, PN_NIL, lineno, line)
  if (step != PN_NIL) {
    /* 'next' jumps to the loop test, so the step runs at the test, not at the end of the body:
     * while ((first ? first = 0 : step) || 1 and cond) */
    char nb[40];
    int nn = snprintf(nb, sizeof nb, "$__cf_%d", ctr++);
    PN first = potion_source(P, AST_MSG, PN_STRN(nb, nn), PN_NIL, PN_NIL, lineno, line);
    PN one = P5_V(AST_VALUE, PN_NUM(1));
    PN setfirst = P5_V(AST_EXPR, PN_TUP(potion_source(P, AST_ASSIGN, first, P5_V(AST_VALUE, PN_ZERO), PN_NIL, lineno, line)));
    PN choose = P5_V(AST_EXPR, PN_PUSH(PN_TUP(
                  potion_source(P, AST_MSG, PN_if, first, P5_V(AST_BLOCK, PN_TUP(setfirst)), lineno, line)),
                  potion_source(P, AST_MSG, PN_else, PN_NIL, P5_V(AST_BLOCK, PN_TUP(step)), lineno, line)));
    PN stepped = potion_source(P, AST_OR, choose, one, PN_NIL, lineno, line);
    if (init != PN_NIL) stmts = PN_PUSH(stmts, init);
    stmts = PN_PUSH(stmts, P5_V(AST_EXPR, PN_TUP(potion_source(P, AST_ASSIGN, first, one, PN_NIL, lineno, line))));
    cond = potion_source(P, AST_AND, stepped, cond, PN_NIL, lineno, line);
    init = PN_NIL;
  }
#undef P5_V
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
  if (PN_IS_PTR(a) && PN_VTYPE(a) == PN_TSOURCE && PN_PART(a) == AST_VALUE) /* `undef // 2`: a bare constant compares wrongly */
    a = potion_source(P, AST_EXPR, PN_TUP(a), PN_NIL, PN_NIL, lineno, line);
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
/* cperl macros (t/comp/macro.t). `macro NAME [:attr] [(sig)] {..}` is run at parse time: the body is
 * compiled and evaluated when the declaration action runs (greg actions run in source order, so
 * before the actions of later calls), the closure is kept in the GC rooted p5_macro_tab and the
 * declaration leaves no code. An operator macro (`macro '+' :infix`) is named ATTR:OP.
 * quasi {..} evaluates to the tuple (BLOCK-ast, unquote-value..); a call site of a macro is
 * replaced IN PLACE (p5_become) by the returned ast, the `unquote` holes of the quasi ast are
 * overwritten in place with the values: no copy of the ast is made, so every expansion works on a fresh copy of the quasi ast (p5_ast_copy). Macro args are passed as their ast. */
static PN p5_sub_proto(Potion *P, long lineno, PN line, PN body);
/* name BLOCK LIST: the call name(sub BLOCK, LIST) */
static PN p5_block_call(Potion *P, long lineno, PN line, PN c, PN b, PN l) {
  PN fm = PN_TUPLE_AT(c, 0), args = PN_TUP(p5_sub_proto(P, lineno, line, b)), sc;
  PN_SIZE i;
  for (i = 0; l != PN_NIL && i < PN_TUPLE_LEN(l); i++) args = PN_PUSH(args, PN_TUPLE_AT(l, i));
  sc = p5_spread_call(P, lineno, line, fm, p5_unparen_args(P, args));
  if (sc != PN_NIL) return potion_source(P, AST_EXPR, sc, PN_NIL, PN_NIL, lineno, line);
  PN_SRC(fm)->a[1] = PN_SRC(potion_source(P, AST_LIST, p5_pad_args(P, fm, args), PN_NIL, PN_NIL, lineno, line)); PN_TOUCH(fm);
  return potion_source(P, AST_EXPR, c, PN_NIL, PN_NIL, lineno, line);
}
static char p5_macros[64][32];
static int p5_macros_n;
static PN p5_macro_tab;
static int p5_macro_idx(const char *name) {
  int i;
  for (i = 0; i < p5_macros_n; i++) if (!strcmp(p5_macros[i], name)) return i;
  return -1;
}
static int p5_is_src(PN n) { return PN_IS_PTR(n) && PN_VTYPE(n) == PN_TSOURCE; }
static PN p5_pat_unq;
static int p5_is_unq(Potion *P, PN n) {
  PN caps[PN_AST_CAPS];
  if (!p5_pat_unq) {
    p5_pat_unq = potion_source(P, AST_MSG, PN_STR("p5unquote"), potion_ast_wild(P, -1), potion_ast_wild(P, -1), 0, PN_NIL);
    potion_gc_root(P, &p5_pat_unq);
  }
  return potion_ast_match(P, p5_pat_unq, n, caps, 0);
}
/* overwrite node dst with the contents of node src */
static void p5_become(Potion *P, PN dst, PN src) {
  int k;
  PN_PART(dst) = PN_PART(src);
  for (k = 0; k < 3; k++) PN_SRC(dst)->a[k] = PN_SRC(PN_S(src, k));
  PN_TOUCH(dst);
}
static PN p5_macro_ast(Potion *P, long lineno, PN line, PN v);
/* the unquote holes of the ast N in pre-order: with VALS == NIL their blocks are pushed to *out,
 * else hole i becomes VALS[i]. Constants (VALUE: also nested quasis) and hole blocks are not entered. */
static int p5_quasi_walk(Potion *P, long lineno, PN line, PN n, PN *out, PN vals, int *pos) {
  int cnt = 0, k;
  PN_SIZE i;
  if (PN_IS_TUPLE(n)) {
    for (i = 0; i < PN_TUPLE_LEN(n); i++) cnt += p5_quasi_walk(P, lineno, line, PN_TUPLE_AT(n, i), out, vals, pos);
    return cnt;
  }
  if (!p5_is_src(n) || PN_PART(n) == AST_VALUE) return 0;
  if (p5_is_unq(P, n)) {
    if (vals == PN_NIL) *out = PN_PUSH(*out, PN_S(n, 2));
    else if (*pos < (int)PN_TUPLE_LEN(vals)) p5_become(P, n, p5_macro_ast(P, lineno, line, PN_TUPLE_AT(vals, (*pos)++)));
    return 1;
  }
  for (k = 0; k < 3; k++) cnt += p5_quasi_walk(P, lineno, line, PN_S(n, k), out, vals, pos);
  return cnt;
}
/* quasi BLOCK => LIST(VALUE(BLOCK), (sub { unquote block })->() ..) */
static PN p5_quasi(Potion *P, long lineno, PN line, PN block) {
  PN holes = PN_TUP0(), items;
  PN_SIZE i;
  int pos = 0;
  p5_quasi_walk(P, lineno, line, block, &holes, PN_NIL, &pos);
  items = PN_TUP(potion_source(P, AST_VALUE, block, PN_NIL, PN_NIL, lineno, line));
  for (i = 0; i < PN_TUPLE_LEN(holes); i++)
    items = PN_PUSH(items, p5_callv_node(P, lineno, line, p5_sub_proto(P, lineno, line, PN_TUPLE_AT(holes, i)),
                      potion_source(P, AST_LIST, PN_NIL, PN_NIL, PN_NIL, lineno, line)));
  return potion_source(P, AST_LIST, items, PN_NIL, PN_NIL, lineno, line);
}
/* a fresh copy of the tree N (nodes and tuples; VALUE nodes and strings are shared): every expansion of a
 * quasi splices its unquote values into its own copy */
static PN p5_ast_copy(Potion *P, PN n) {
  PN_SIZE i;
  if (PN_IS_TUPLE(n)) {
    PN t = PN_TUP0();
    for (i = 0; i < PN_TUPLE_LEN(n); i++) t = PN_PUSH(t, p5_ast_copy(P, PN_TUPLE_AT(n, i)));
    return t;
  }
  if (p5_is_src(n) && PN_PART(n) != AST_VALUE) {
    PN c = potion_source(P, PN_PART(n), PN_NIL, PN_NIL, PN_NIL, ((struct PNSource *)n)->loc.lineno, PN_NIL);
    int k;
    for (k = 0; k < 3; k++) { PN ck = p5_ast_copy(P, PN_S(n, k)); PN_SRC(c)->a[k] = PN_SRC(ck); PN_TOUCH(c); }
    PN_TOUCH(c);
    return c;
  }
  return n;
}
/* the ast for the value V returned by a macro (or an unquote): a quasi tuple is spliced, an ast is
 * used as it is, any other value is a constant */
static PN p5_macro_ast(Potion *P, long lineno, PN line, PN v) {
  if (PN_IS_TUPLE(v) && PN_TUPLE_LEN(v) >= 1 && p5_is_src(PN_TUPLE_AT(v, 0)) && PN_PART(PN_TUPLE_AT(v, 0)) == AST_BLOCK) {
    PN blk = p5_ast_copy(P, PN_TUPLE_AT(v, 0)), vals = PN_TUP0(), stmts;
    PN_SIZE i;
    int pos = 0, holes;
    for (i = 1; i < PN_TUPLE_LEN(v); i++) vals = PN_PUSH(vals, PN_TUPLE_AT(v, i));
    holes = p5_quasi_walk(P, lineno, line, blk, NULL, vals, &pos);
    if (holes != (int)PN_TUPLE_LEN(vals)) potion_fatal("macro: the quasi has a different number of unquotes than values");
    stmts = PN_S(blk, 0);
    if (!PN_IS_TUPLE(stmts) || PN_TUPLE_LEN(stmts) == 0) return potion_source(P, AST_VALUE, PN_NIL, PN_NIL, PN_NIL, lineno, line);
    if (PN_TUPLE_LEN(stmts) == 1) return PN_TUPLE_AT(stmts, 0);
    return p5_callv_node(P, lineno, line, p5_sub_proto(P, lineno, line, blk),
                         potion_source(P, AST_LIST, PN_NIL, PN_NIL, PN_NIL, lineno, line));
  }
  if (p5_is_src(v)) return v;
  return potion_source(P, AST_VALUE, v, PN_NIL, PN_NIL, lineno, line);
}
/* user operators: `macro '!!' :prefix|:postfix|:infix (..)` and `macro comet :infix (..)`. The grammar must know them
 * while it parses, but a macro is defined by an action after the parse: the source is scanned first. */
static struct { char text[24]; int kind; } p5_mops[32]; /* kind 0 prefix, 1 postfix, 2 infix */
static int p5_mops_n;
static void p5_prescan_macros(const char *src, long n) {
  long i = 0;
  while (i < n) {
    long e = i, k;
    while (e < n && src[e] != '\n') e++;
    k = i;
    while (k < e && (src[k] == ' ' || src[k] == '\t')) k++;
    if (e - k > 6 && !strncmp(src + k, "macro", 5) && (src[k + 5] == ' ' || src[k + 5] == '\t')) {
      long b = k + 5, f, a;
      char text[24];
      int kind = -1, quoted = 0, l;
      while (b < e && (src[b] == ' ' || src[b] == '\t')) b++;
      if (b < e && (src[b] == '\'' || src[b] == '"')) { quoted = 1; b++; f = b; while (f < e && src[f] != src[b - 1]) f++; }
      else { f = b; while (f < e && (isalnum((unsigned char)src[f]) || src[f] == '_')) f++; }
      l = (int)(f - b);
      a = f + quoted;
      while (a < e && src[a] != ':') a++;
      if (l > 0 && l < 24 && a + 1 < e) {
        if (!strncmp(src + a, ":prefix", 7)) kind = 0;
        else if (!strncmp(src + a, ":postfix", 8)) kind = 1;
        else if (!strncmp(src + a, ":infix", 6)) kind = 2;
      }
      if (kind >= 0) {
        int dup = 0, j;
        memcpy(text, src + b, l); text[l] = 0;
        /* the builtin binary operators are handled by p5_opmacro */
        if (kind == 2 && (!strcmp(text, "+") || !strcmp(text, "-") || !strcmp(text, "*") || !strcmp(text, "/") || !strcmp(text, ".") ||
            !strcmp(text, "==") || !strcmp(text, "<") || !strcmp(text, ">") || !strcmp(text, "<=") || !strcmp(text, ">="))) dup = 1;
        for (j = 0; j < p5_mops_n; j++) if (p5_mops[j].kind == kind && !strcmp(p5_mops[j].text, text)) dup = 1;
        if (!dup && p5_mops_n < 32) { strcpy(p5_mops[p5_mops_n].text, text); p5_mops[p5_mops_n++].kind = kind; }
      }
    }
    i = e + 1;
  }
}
/* the expansion of the user operator OP of the macro kinds above applied to ARGS (a tuple of asts) */
static PN p5_mop_call(Potion *P, long lineno, PN line, const char *kind, PN op, PN args) {
  char nm[40];
  PN m;
  snprintf(nm, sizeof nm, "%s:%s", kind, PN_STR_PTR(op));
  if (p5_macro_idx(nm) < 0) snprintf(nm, sizeof nm, "%s", PN_STR_PTR(op));
  m = potion_source(P, AST_MSG, potion_str(P, nm), PN_NIL, PN_NIL, lineno, line);
  if (!p5_macro_call(P, lineno, line, m, args)) potion_fatal("macro operator without a definition");
  return potion_source(P, AST_EXPR, PN_TUP(m), PN_NIL, PN_NIL, lineno, line);
}
/* `a + b` with a user `macro '+' :infix (..)`: the macro gets the asts of both operands (left, right) and
 * the expression is replaced by what it returns */
static PN p5_opmacro(Potion *P, long lineno, PN line, const char *op, PN a, PN b, PN fallback) {
  char nm[40];
  PN m;
  snprintf(nm, sizeof nm, "infix:%s", op);
  if (p5_macro_idx(nm) < 0) return fallback;
  m = potion_source(P, AST_MSG, potion_str(P, nm), PN_NIL, PN_NIL, lineno, line);
  if (!p5_macro_call(P, lineno, line, m, PN_PUSH(PN_TUP(a), b))) return fallback;
  return potion_source(P, AST_EXPR, PN_TUP(m), PN_NIL, PN_NIL, lineno, line);
}
static PN p5_macro_def(Potion *P, long lineno, PN line, PN name, PN attr, PN sig, PN body) {
  PN proto = sig == PN_NIL ? p5_sub_proto(P, lineno, line, body) : potion_source(P, AST_PROTO, sig, body, PN_NIL, lineno, line);
  PN cl;
  if (attr != PN_NIL && !isalpha((unsigned char)PN_STR_PTR(name)[0]) && PN_STR_PTR(name)[0] != '_') {
    char buf[96];
    int len = snprintf(buf, sizeof buf, "%s:%s", PN_STR_PTR(attr), PN_STR_PTR(name));
    name = potion_str2(P, buf, len);
  }
  cl = p2_eval(P, potion_source(P, AST_BLOCK, PN_TUP(potion_source(P, AST_EXPR, PN_TUP(proto), PN_NIL, PN_NIL, lineno, line)),
                                PN_NIL, PN_NIL, lineno, line));
  if (!p5_macro_tab) { p5_macro_tab = PN_TUP0(); potion_gc_root(P, &p5_macro_tab); }
  if (PN_STR_LEN(name) < 32 && p5_macros_n < 64 && p5_macro_idx(PN_STR_PTR(name)) < 0) {
    strcpy(p5_macros[p5_macros_n++], PN_STR_PTR(name));
    p5_macro_tab = PN_PUSH(p5_macro_tab, cl);
  }
  return PN_TUP0();
}
/* call site M (a MSG node) of NAME with the arg asts ARGS (tuple or NIL): if NAME is a macro run it
 * and replace M in place by the expansion; returns 1 if it was a macro */
static int p5_macro_call(Potion *P, long lineno, PN line, PN m, PN args) {
  PN vargs = PN_TUP0(), v, r;
  PN_SIZE i;
  int idx;
  if (!PN_IS_STR(PN_S(m, 0)) || (idx = p5_macro_idx(PN_STR_PTR(PN_S(m, 0)))) < 0) return 0;
  if (PN_IS_TUPLE(args))
    for (i = 0; i < PN_TUPLE_LEN(args); i++)
      vargs = PN_PUSH(vargs, potion_source(P, AST_VALUE, PN_TUPLE_AT(args, i), PN_NIL, PN_NIL, lineno, line));
  v = p2_eval(P, potion_source(P, AST_BLOCK, PN_TUP(
        p5_callv_node(P, lineno, line, potion_source(P, AST_VALUE, PN_TUPLE_AT(p5_macro_tab, idx), PN_NIL, PN_NIL, lineno, line),
                      potion_source(P, AST_LIST, vargs, PN_NIL, PN_NIL, lineno, line))), PN_NIL, PN_NIL, lineno, line));
  r = p5_macro_ast(P, lineno, line, v);
  p5_become(P, m, r);
  return 1;
}

/* END blocks run after the program in reverse order of definition: the blocks are collected
 * (also from required files) and appended to the statements of the main program. A die or an
 * exit skips them. */
/* an identifier: the unicode identifier checks (the old ' package separator is gone) */
static PN p5_idname(Potion *P, const char *file, long line, const char *t, int n) {
  return p5_uni_warn_at(P, file, line, t, n);
}
static PN p5_end_blocks, p5_check_blocks, p5_init_blocks;
static int p5_end_rooted, p5_phase_used;
static void p5_end_root(Potion *P) {
  if (p5_end_rooted) return;
  /* root the variables before allocating: the second PN_TUP0() may collect the first one */
  p5_end_blocks = p5_check_blocks = p5_init_blocks = PN_NIL;
  potion_gc_root(P, &p5_end_blocks); potion_gc_root(P, &p5_check_blocks); potion_gc_root(P, &p5_init_blocks);
  p5_end_rooted = 1;
  p5_end_blocks = PN_TUP0(); p5_check_blocks = PN_TUP0(); p5_init_blocks = PN_TUP0();
}
static void p5_end_add(Potion *P, PN block) { p5_end_root(P); p5_end_blocks = PN_PUSH(p5_end_blocks, block); }
/* CHECK blocks run in reverse order before the main program, INIT blocks in order */
static void p5_check_add(Potion *P, PN block) { p5_end_root(P); p5_check_blocks = PN_PUSH(p5_check_blocks, block); }
static void p5_init_add(Potion *P, PN block) { p5_end_root(P); p5_init_blocks = PN_PUSH(p5_init_blocks, block); }
/* ${^GLOBAL_PHASE} = 'NAME' */
static PN p5_phase_set(Potion *P, const char *name) {
  return potion_source(P, AST_ASSIGN, potion_source(P, AST_MSG, PN_STR("$^GLOBAL_PHASE"), PN_NIL, PN_NIL, 0, PN_NIL),
                       potion_source(P, AST_VALUE, PN_STR(name), PN_NIL, PN_NIL, 0, PN_NIL), PN_NIL, 0, PN_NIL);
}
static PN p5_end_append(Potion *P, PN stmts) {
  PN_SIZE i, n;
  PN out;
  if (!p5_end_rooted || p5_parse_depth > 1) return stmts;
  if (PN_TUPLE_LEN(p5_end_blocks) == 0 && PN_TUPLE_LEN(p5_check_blocks) == 0 && PN_TUPLE_LEN(p5_init_blocks) == 0 && !p5_phase_used) return stmts;
  out = PN_TUP0();
  if (p5_phase_used) out = PN_PUSH(out, p5_phase_set(P, "CHECK"));
  n = PN_TUPLE_LEN(p5_check_blocks);
  for (i = n; i > 0; i--) out = PN_PUSH(out, PN_TUPLE_AT(p5_check_blocks, i - 1));
  if (p5_phase_used) out = PN_PUSH(out, p5_phase_set(P, "INIT"));
  n = PN_TUPLE_LEN(p5_init_blocks);
  for (i = 0; i < n; i++) out = PN_PUSH(out, PN_TUPLE_AT(p5_init_blocks, i));
  if (p5_phase_used) out = PN_PUSH(out, p5_phase_set(P, "RUN"));
  if (PN_IS_TUPLE(stmts)) for (i = 0; i < PN_TUPLE_LEN(stmts); i++) out = PN_PUSH(out, PN_TUPLE_AT(stmts, i));
  if (p5_phase_used) out = PN_PUSH(out, p5_phase_set(P, "END"));
  n = PN_TUPLE_LEN(p5_end_blocks);
  for (i = n; i > 0; i--) out = PN_PUSH(out, PN_TUPLE_AT(p5_end_blocks, i - 1));
  p5_end_blocks = PN_TUP0(); p5_check_blocks = PN_TUP0(); p5_init_blocks = PN_TUP0();
  p5_phase_used = 0; /* only the main program: a later eval STRING must not run the phases again */
  return out;
}

/* Perl subs see their arguments as @_. The VM has no varargs, so a plain
 * 'sub f {...}' whose body mentions @_ (shift, pop, $_[N] are rewritten to
 * @_ by the grammar) is compiled with P5_NARGS optional parameters
 * $__a0.. defaulting to the PN_NONE sentinel, and a prologue
 *   @_ = p5args($__a0, ..., $__aN);
 * which drops the unfilled tail (see p5_args in lib/p5/p5table.c).
 * Calls with more than P5_NARGS arguments lose the rest. */
#define P5_NARGS 12

/* does the sub body mention @_ (not in a nested sub, which has its own)? */
static int p5_uses_args(Potion *P, PN t) {
  PN caps[PN_AST_CAPS];
  PN pat = potion_source(P, AST_MSG, PN_STR("@_"), potion_ast_wild(P, -1), PN_NIL, 0, PN_NIL);
  return potion_ast_find(P, pat, t, caps, PN_AST_NOPROTO);
}

/* Outside of any sub a bare shift/pop (the grammar emits them on @_) works on
 * @ARGV: rewrite every @_ that is not inside a sub body. */
static PN p5_pat_args;
static void p5_top_args_node(Potion *P, PN node, void *ud) {
  PN caps[PN_AST_CAPS];
  if (potion_ast_match(P, p5_pat_args, node, caps, 0)) {
    PN_SRC(node)->a[0] = PN_SRC(*(PN *)ud); /* made before the walk: it must not allocate */
    PN_TOUCH(node);
  }
}
static void p5_top_args(Potion *P, PN t) {
  if (!p5_pat_args) {
    p5_pat_args = potion_source(P, AST_MSG, PN_STR("@_"), potion_ast_wild(P, -1), potion_ast_wild(P, -1), 0, PN_NIL);
    potion_gc_root(P, &p5_pat_args);
  }
  { PN argv = PN_STR("@ARGV"); potion_ast_walk(P, t, PN_AST_NOPROTO, p5_top_args_node, (void *)&argv); }
}

static void p5_note_args(Potion *P) {
  /* The callee reads its P5_NARGS hidden parameters whatever the caller passed:
   * the x86-64 JIT pads every call site with PN_P5NOARG up to P5_NARGS
   * arguments (P->p5args). Other targets fill missing optional arguments at the
   * call site from protos[0]'s signature (not the callee's) and have too small
   * an outgoing argument area: run those in the bytecode VM, which binds the
   * defaults from the callee's own signature. */
#if defined(POTION_JIT_TARGET) && POTION_JIT_TARGET == POTION_X86 && PN_SIZE_T == 8
  P->p5args = 1;
#else
  if ((P->flags & ((1 << EXEC_BITS) - 1)) == EXEC_JIT)
    P->flags = (Potion_Flags)((P->flags & ~((1 << EXEC_BITS) - 1)) | EXEC_VM);
#endif
}

/* `my $x = shift;` as a body statement: the name of $x, else NIL. */
static PN p5_shift_stmt(Potion *P, PN st) {
  PN caps[PN_AST_CAPS];
  PN pat = potion_source(P, AST_ASSIGN,
    potion_source(P, AST_MSG, potion_ast_wild(P, 0), PN_NIL, PN_NIL, 0, PN_NIL),
    potion_source(P, AST_EXPR, PN_PUSH(PN_PUSH(PN_TUP0(),
        potion_source(P, AST_MSG, PN_STR("@_"), PN_NIL, PN_NIL, 0, PN_NIL)),
        potion_source(P, AST_MSG, PN_STR("shift"), PN_NIL, PN_NIL, 0, PN_NIL)), PN_NIL, PN_NIL, 0, PN_NIL),
    PN_NIL, 0, PN_NIL);
  if (!potion_ast_match(P, pat, st, caps, PN_AST_PEEL) || !PN_IS_STR(caps[0]) || PN_STR_PTR(caps[0])[0] != '$')
    return PN_NIL;
  return caps[0];
}

/* `my ($a, $b) = @_;` as a body statement, as p5_aasign lowered it, element by element
 * (a lone @_ on the right is read directly):
 *   EXPR[ ASSIGN($a, @_[0]), ASSIGN($b, @_[1]), .. ]
 * the tuple of the names if all targets are plain scalars taking @_[0], @_[1], .. in order
 * (no undef placeholder, no array or hash slurping the rest), else NIL. */
static PN p5_myargs_stmt(Potion *P, PN st) {
  PN caps[PN_AST_CAPS], names = PN_TUP0(), list;
  PN_SIZE i;
  /* one element: ASSIGN($name, @_(LIST[VALUE(i)])) */
  PN elem = potion_source(P, AST_ASSIGN,
    potion_source(P, AST_MSG, potion_ast_wild(P, 0), PN_NIL, PN_NIL, 0, PN_NIL),
    potion_source(P, AST_MSG, PN_STR("@_"),
      potion_source(P, AST_LIST, PN_PUSH(PN_TUP0(),
        potion_source(P, AST_VALUE, potion_ast_wild(P, 1), PN_NIL, PN_NIL, 0, PN_NIL)),
        PN_NIL, PN_NIL, 0, PN_NIL), PN_NIL, 0, PN_NIL),
    PN_NIL, 0, PN_NIL);
  while (PN_PART(st) == AST_EXPR && PN_IS_TUPLE(PN_S(st, 0)) && PN_TUPLE_LEN(PN_S(st, 0)) == 1 &&
         PN_PART(PN_TUPLE_AT(PN_S(st, 0), 0)) == AST_EXPR)
    st = PN_TUPLE_AT(PN_S(st, 0), 0);
  if (PN_PART(st) != AST_EXPR || !PN_IS_TUPLE(PN_S(st, 0)) || PN_TUPLE_LEN(PN_S(st, 0)) == 0)
    return PN_NIL;
  list = PN_S(st, 0);
  for (i = 0; i < PN_TUPLE_LEN(list); i++) {
    if (!potion_ast_match(P, elem, PN_TUPLE_AT(list, i), caps, PN_AST_PEEL) ||
        !PN_IS_STR(caps[0]) || PN_STR_PTR(caps[0])[0] != '$' || caps[1] != PN_NUM(i))
      return PN_NIL;
    names = PN_PUSH(names, caps[0]);
  }
  return names;
}

static PN p5_sub_proto(Potion *P, long lineno, PN line, PN body) {
  PN sig = PN_TUP0(), args = PN_TUP0(), stmts, call, assign;
  int i;
  char nm[16];
  if (PN_S(body, 0) == PN_NIL || (PN_IS_TUPLE(PN_S(body, 0)) && PN_TUPLE_LEN(PN_S(body, 0)) == 0)) /* sub f {}: returns undef */
    body = potion_source(P, AST_BLOCK, PN_TUP(potion_source(P, AST_EXPR, PN_TUP(potion_source(P, AST_VALUE, PN_NIL, PN_NIL, PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line);
  if (p5_want_used && PN_S(body, 0) != PN_NIL && PN_IS_TUPLE(PN_S(body, 0))) { /* my $__wa = the caller's context */
    PN caps[PN_AST_CAPS];
    PN pat = potion_source(P, AST_MSG, PN_STR("$__wa"), potion_ast_wild(P, -1), PN_NIL, 0, PN_NIL);
    if (potion_ast_find(P, pat, body, caps, PN_AST_NOPROTO)) {
      PN ws = potion_source(P, AST_ASSIGN, potion_source(P, AST_MSG, PN_STR("$__wa"), PN_NIL, PN_NIL, lineno, line),
                potion_source(P, AST_EXPR, PN_TUP(p5_want_call(P, lineno, line, -1)), PN_NIL, PN_NIL, lineno, line), PN_NIL, lineno, line);
      PN nb = PN_TUP(ws), old0 = PN_S(body, 0);
      for (i = 0; i < (int)PN_TUPLE_LEN(old0); i++) nb = PN_PUSH(nb, PN_TUPLE_AT(old0, i));
      body = potion_source(P, AST_BLOCK, nb, PN_NIL, PN_NIL, lineno, line);
    }
  }
  if (!p5_uses_args(P, body))
    return potion_source(P, AST_PROTO, potion_source(P, AST_LIST, PN_NIL, PN_NIL,
                         PN_NIL, lineno, line), body, PN_NIL, lineno, line);
  p5_note_args(P);
  /* The usual `sub f { my $a = shift; my $b = shift; ... }` where @_ is not
   * mentioned afterwards: the shifts are plain optional parameters, no hidden
   * parameters, @_ tuple or shift sends. A missing argument arrives as
   * PN_P5NOARG (the jit pads call sites with it), which is undef. */
  if (PN_S(body, 0) != PN_NIL && PN_IS_TUPLE(PN_S(body, 0))) {
    PN old = PN_S(body, 0), names = PN_TUP0(), rest = PN_TUP0();
    int n = 0, len = (int)PN_TUPLE_LEN(old);
    int nstmt = 0; /* leading statements taken */
    while (nstmt < len && n < P5_NARGS && p5_shift_stmt(P, PN_TUPLE_AT(old, nstmt)) != PN_NIL) {
      names = PN_PUSH(names, p5_shift_stmt(P, PN_TUPLE_AT(old, nstmt)));
      n++; nstmt++;
    }
    /* `my ($a, $b) = @_;` takes the remaining arguments: it can only be the last one, and
     * there must be a statement after it (it would be the value of the sub) */
    if (nstmt + 1 < len) {
      PN mn = p5_myargs_stmt(P, PN_TUPLE_AT(old, nstmt));
      if (mn != PN_NIL && n + (int)PN_TUPLE_LEN(mn) <= P5_NARGS) {
        for (i = 0; i < (int)PN_TUPLE_LEN(mn); i++) names = PN_PUSH(names, PN_TUPLE_AT(mn, i));
        n += (int)PN_TUPLE_LEN(mn); nstmt++;
      }
    }
    if (n > 0) {
      PN tail = PN_TUP0();
      int rest_uses = 0;
      for (i = nstmt; i < len; i++) {
        tail = PN_PUSH(tail, PN_TUPLE_AT(old, i));
        if (p5_uses_args(P, PN_TUPLE_AT(old, i))) rest_uses = 1;
      }
      if (!rest_uses) {
        for (i = 0; i < n; i++) {
          PN nm = PN_TUPLE_AT(names, i), ref, cond, set;
          sig = PN_PUSH(PN_PUSH(PN_PUSH(sig, nm), PN_NUM(':')), PN_P5NOARG);
          /* $x = undef if $x == PN_P5NOARG */
          ref = potion_source(P, AST_EXPR, PN_TUP(potion_source(P, AST_MSG, nm, PN_NIL, PN_NIL, lineno, line)),
                              PN_NIL, PN_NIL, lineno, line);
          cond = potion_source(P, AST_EQ, ref, potion_source(P, AST_VALUE, PN_P5NOARG, PN_NIL, PN_NIL, lineno, line),
                               PN_NIL, lineno, line);
          set = potion_source(P, AST_ASSIGN,
                  potion_source(P, AST_EXPR, PN_TUP(potion_source(P, AST_MSG, nm, PN_NIL, PN_NIL, lineno, line)),
                                PN_NIL, PN_NIL, lineno, line),
                  potion_source(P, AST_VALUE, PN_NIL, PN_NIL, PN_NIL, lineno, line), PN_NIL, lineno, line);
          rest = PN_PUSH(rest, potion_source(P, AST_AND, cond, set, PN_NIL, lineno, line));
        }
        for (i = 0; i < (int)PN_TUPLE_LEN(tail); i++) rest = PN_PUSH(rest, PN_TUPLE_AT(tail, i));
        return potion_source(P, AST_PROTO, potion_source(P, AST_LIST, sig, PN_NIL, PN_NIL, lineno, line),
                 potion_source(P, AST_BLOCK, rest, PN_NIL, PN_NIL, lineno, line), PN_NIL, lineno, line);
      }
    }
  }

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
           potion_source(P, AST_LIST, args, PN_NIL, PN_NIL, lineno, line),
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
static PN p5_pat_paren;
static PN p5_unparen(Potion *P, PN e) {
  PN caps[PN_AST_CAPS];
  if (!p5_pat_paren) { /* EXPR[ LIST[ ?0 ] ] */
    p5_pat_paren = potion_source(P, AST_EXPR, PN_TUP(potion_source(P, AST_LIST,
                     PN_TUP(potion_ast_wild(P, 0)), PN_NIL, PN_NIL, 0, PN_NIL)), PN_NIL, PN_NIL, 0, PN_NIL);
    potion_gc_root(P, &p5_pat_paren);
  }
  while (potion_ast_match(P, p5_pat_paren, e, caps, 0)) e = caps[0];
  return e;
}

/* a condition that is a plain array (`if @a`, `@a ? x : y`): true iff non-empty,
 * so test its length. Everything else is unchanged. */
static PN p5_pat_cond, p5_pat_cond_arr;
static PN p5_cond(Potion *P, long lineno, PN line, PN e);
#define P5N(e) p5_cond(P, G->lineno, P->line, e) /* an operand in scalar context: @a is its length */
/* the rhs of `L = E`: a plain array is its length only when L is a scalar */
static PN p5_scalar_rhs(Potion *P, long lineno, PN line, PN l, PN e) {
  int scalar = PN_IS_PTR(l) && PN_VTYPE(l) == PN_TSOURCE && PN_PART(l) == AST_MSG && PN_IS_STR(PN_S(l, 0)) && PN_STR_PTR(PN_S(l, 0))[0] == '$';
  return scalar ? p5_cond(P, lineno, line, e) : p5_unparen(P, p5_listctx(P, e));
}
/* is E the EXPR[MSG if, MSG else] of C ? X : Y */
static int p5_is_ternary(PN e) {
  PN u = e, a, b;
  while (PN_IS_PTR(u) && PN_VTYPE(u) == PN_TSOURCE && PN_PART(u) == AST_EXPR && PN_IS_TUPLE(PN_S(u, 0)) && PN_TUPLE_LEN(PN_S(u, 0)) == 1) u = PN_TUPLE_AT(PN_S(u, 0), 0);
  if (!(PN_IS_PTR(u) && PN_VTYPE(u) == PN_TSOURCE && PN_PART(u) == AST_EXPR && PN_IS_TUPLE(PN_S(u, 0)) && PN_TUPLE_LEN(PN_S(u, 0)) == 2)) return 0;
  a = PN_TUPLE_AT(PN_S(u, 0), 0); b = PN_TUPLE_AT(PN_S(u, 0), 1);
  return PN_IS_PTR(a) && PN_VTYPE(a) == PN_TSOURCE && PN_PART(a) == AST_MSG && PN_IS_STR(PN_S(a, 0)) && !strcmp(PN_STR_PTR(PN_S(a, 0)), "if") &&
         PN_IS_PTR(b) && PN_VTYPE(b) == PN_TSOURCE && PN_PART(b) == AST_MSG && PN_IS_STR(PN_S(b, 0)) && !strcmp(PN_STR_PTR(PN_S(b, 0)), "else");
}
/* wantarray ? LIST : SCALAR: the false branch is in scalar context, a slice there is its last element */
static int p5_is_wantcond(PN c) {
  while (PN_IS_PTR(c) && PN_VTYPE(c) == PN_TSOURCE && PN_PART(c) == AST_EXPR && PN_IS_TUPLE(PN_S(c, 0)) && PN_TUPLE_LEN(PN_S(c, 0)) == 1) c = PN_TUPLE_AT(PN_S(c, 0), 0);
  return PN_IS_PTR(c) && PN_VTYPE(c) == PN_TSOURCE && PN_PART(c) == AST_MSG && PN_IS_STR(PN_S(c, 0)) && !strcmp(PN_STR_PTR(PN_S(c, 0)), "$__wa");
}
static PN p5_scalar_branch(Potion *P, PN f) {
  PN u = f, last;
  while (PN_IS_PTR(u) && PN_VTYPE(u) == PN_TSOURCE && PN_PART(u) == AST_EXPR && PN_IS_TUPLE(PN_S(u, 0)) && PN_TUPLE_LEN(PN_S(u, 0)) == 1 &&
         PN_IS_PTR(PN_TUPLE_AT(PN_S(u, 0), 0)) && PN_VTYPE(PN_TUPLE_AT(PN_S(u, 0), 0)) == PN_TSOURCE && PN_PART(PN_TUPLE_AT(PN_S(u, 0), 0)) == AST_EXPR)
    u = PN_TUPLE_AT(PN_S(u, 0), 0);
  if (!(PN_IS_PTR(u) && PN_VTYPE(u) == PN_TSOURCE && PN_PART(u) == AST_EXPR && PN_IS_TUPLE(PN_S(u, 0)) && PN_TUPLE_LEN(PN_S(u, 0)) >= 2)) return f;
  last = PN_TUPLE_AT(PN_S(u, 0), PN_TUPLE_LEN(PN_S(u, 0)) - 1);
  if (PN_IS_PTR(last) && PN_VTYPE(last) == PN_TSOURCE && PN_PART(last) == AST_MSG && PN_IS_STR(PN_S(last, 0)) &&
      (!strcmp(PN_STR_PTR(PN_S(last, 0)), "p5slice") || !strcmp(PN_STR_PTR(PN_S(last, 0)), "p5sliceof")))
    return potion_source(P, AST_EXPR, PN_PUSH(PN_S(u, 0), potion_source(P, AST_MSG, PN_STR("p5lastel"), PN_NIL, PN_NIL, 0, PN_NIL)), PN_NIL, PN_NIL, 0, PN_NIL);
  return f;
}
/* `each %h` in scalar context: the key (array: the index), not the (key, value) pair.
 * Appends p5eachk to the chain; list contexts (p5_listctx, list assignments) keep the pair. */
static PN p5_each_scalar(Potion *P, long lineno, PN line, PN u) {
  if (!(PN_IS_PTR(u) && PN_VTYPE(u) == PN_TSOURCE && PN_PART(u) == AST_EXPR && PN_IS_TUPLE(PN_S(u, 0))
        && PN_TUPLE_LEN(PN_S(u, 0)) >= 2)) return u;
  PN m = PN_TUPLE_AT(PN_S(u, 0), PN_TUPLE_LEN(PN_S(u, 0)) - 1);
  if (!(PN_IS_PTR(m) && PN_VTYPE(m) == PN_TSOURCE && PN_PART(m) == AST_MSG && PN_IS_STR(PN_S(m, 0)) && PN_S(m, 1) == PN_NIL
        && !strcmp(PN_STR_PTR(PN_S(m, 0)), "p5each"))) return u;
  return potion_source(P, AST_EXPR, PN_PUSH(PN_S(u, 0), potion_source(P, AST_MSG, PN_STR("p5eachk"), PN_NIL, PN_NIL, lineno, line)),
                       PN_NIL, PN_NIL, lineno, line);
}
static PN p5_cond(Potion *P, long lineno, PN line, PN e) {
  PN caps[PN_AST_CAPS], u = p5_unparen(P, e);
  if (!p5_pat_cond) { /* EXPR[ ?0 ] and a plain array MSG "@*" without args */
    p5_pat_cond = potion_source(P, AST_EXPR, PN_TUP(potion_ast_wild(P, 0)), PN_NIL, PN_NIL, 0, PN_NIL); potion_gc_root(P, &p5_pat_cond);
    p5_pat_cond_arr = potion_source(P, AST_MSG, PN_STR("@*"), PN_NIL, potion_ast_wild(P, -1), 0, PN_NIL);
    potion_gc_root(P, &p5_pat_cond_arr);
  }
  if (PN_IS_PTR(u) && PN_VTYPE(u) == PN_TSOURCE && PN_PART(u) == AST_EXPR && PN_IS_TUPLE(PN_S(u, 0)) && PN_TUPLE_LEN(PN_S(u, 0)) == 1) {
    PN l = PN_TUPLE_AT(PN_S(u, 0), 0); /* `()` in scalar context: undef */
    if (PN_IS_PTR(l) && PN_VTYPE(l) == PN_TSOURCE && PN_PART(l) == AST_LIST && (PN_S(l, 0) == PN_NIL || (PN_IS_TUPLE(PN_S(l, 0)) && PN_TUPLE_LEN(PN_S(l, 0)) == 0)))
      return potion_source(P, AST_VALUE, PN_NIL, PN_NIL, PN_NIL, lineno, line);
  }
  if (potion_ast_match(P, p5_pat_cond, u, caps, 0) && potion_ast_match(P, p5_pat_cond_arr, caps[0], caps, 0)) {
    PN m = PN_TUPLE_AT(PN_S(u, 0), 0);
    return potion_source(P, AST_EXPR, PN_PUSH(PN_TUP(m),
             potion_source(P, AST_MSG, PN_STR("p5alen"), PN_NIL, PN_NIL, lineno, line)),
             PN_NIL, PN_NIL, lineno, line);
  }
  if (PN_IS_PTR(u) && PN_VTYPE(u) == PN_TSOURCE && PN_PART(u) == AST_EXPR && PN_IS_TUPLE(PN_S(u, 0)) && PN_TUPLE_LEN(PN_S(u, 0)) == 2) {
    PN a = PN_TUPLE_AT(PN_S(u, 0), 0), b = PN_TUPLE_AT(PN_S(u, 0), 1); /* C ? X : Y in scalar context: each branch is */
    if (PN_IS_PTR(a) && PN_VTYPE(a) == PN_TSOURCE && PN_PART(a) == AST_MSG && PN_IS_STR(PN_S(a, 0)) && !strcmp(PN_STR_PTR(PN_S(a, 0)), "if") &&
        PN_IS_PTR(b) && PN_VTYPE(b) == PN_TSOURCE && PN_PART(b) == AST_MSG && PN_IS_STR(PN_S(b, 0)) && !strcmp(PN_STR_PTR(PN_S(b, 0)), "else")) {
      PN ms[2] = { a, b };
      int k;
      for (k = 0; k < 2; k++) {
        PN blk = PN_S(ms[k], 2);
        if (PN_IS_PTR(blk) && PN_VTYPE(blk) == PN_TSOURCE && PN_PART(blk) == AST_BLOCK && PN_IS_TUPLE(PN_S(blk, 0)) && PN_TUPLE_LEN(PN_S(blk, 0)) == 1) {
          PN st = PN_S(blk, 0);
          PN it = PN_TUPLE_AT(st, 0);
          while (PN_IS_PTR(it) && PN_VTYPE(it) == PN_TSOURCE && PN_PART(it) == AST_EXPR && PN_IS_TUPLE(PN_S(it, 0)) && PN_TUPLE_LEN(PN_S(it, 0)) == 1 &&
                 PN_IS_PTR(PN_TUPLE_AT(PN_S(it, 0), 0)) && PN_VTYPE(PN_TUPLE_AT(PN_S(it, 0), 0)) == PN_TSOURCE && PN_PART(PN_TUPLE_AT(PN_S(it, 0), 0)) == AST_EXPR)
            it = PN_TUPLE_AT(PN_S(it, 0), 0); /* EXPR[EXPR[@a]]: the parenthesis wrapper of the branch */
          PN_TUPLE_AT(st, 0) = p5_cond(P, lineno, line, it);
          PN_TOUCH(st);
        }
      }
      return u;
    }
  }
  if (PN_IS_PTR(u) && PN_VTYPE(u) == PN_TSOURCE && PN_PART(u) == AST_EXPR && PN_IS_TUPLE(PN_S(u, 0)) && PN_TUPLE_LEN(PN_S(u, 0)) >= 2) {
    PN m = PN_TUPLE_AT(PN_S(u, 0), PN_TUPLE_LEN(PN_S(u, 0)) - 1); /* keys %h / values %h in scalar context: the count */
    if (PN_IS_PTR(m) && PN_VTYPE(m) == PN_TSOURCE && PN_PART(m) == AST_MSG && PN_IS_STR(PN_S(m, 0)) && PN_S(m, 1) == PN_NIL &&
        (!strcmp(PN_STR_PTR(PN_S(m, 0)), "keys") || !strcmp(PN_STR_PTR(PN_S(m, 0)), "values")))
      return potion_source(P, AST_EXPR, PN_PUSH(PN_S(u, 0), potion_source(P, AST_MSG, PN_STR("length"), PN_NIL, PN_NIL, lineno, line)),
               PN_NIL, PN_NIL, lineno, line);
    u = p5_each_scalar(P, lineno, line, u); /* each %h in scalar context: the key */
  }
  if (PN_IS_PTR(u) && PN_VTYPE(u) == PN_TSOURCE && PN_PART(u) == AST_ASSIGN && PN_IS_PTR(PN_S(u, 1))) { /* $k = each %h in a condition: scalar rhs */
    PN r = p5_each_scalar(P, lineno, line, PN_S(u, 1));
    if (r != PN_S(u, 1)) { PN_SRC(u)->a[1] = PN_SRC(r); PN_TOUCH(u); }
  }
  return u;
}
/* bare `each` / `keys` / `values` (no %h / @a operand): perl dies with
 * "Not enough arguments for each"; the other named unary operators operate on $_ */
static PN p5_bare_unary(Potion *P, long lineno, PN line, PN u) {
  PN un = PN_S(u, 0);
  const char *nm = PN_IS_STR(un) ? PN_STR_PTR(un) : 0;
  if (nm && (!strcmp(nm, "keys") || !strcmp(nm, "values") || !strcmp(nm, "p5each"))) {
    char buf[64];
    snprintf(buf, sizeof buf, "Not enough arguments for %s", !strcmp(nm, "p5each") ? "each" : nm);
    return potion_source(P, AST_EXPR,
                         PN_PUSH(PN_TUP(p5_str_value(P, PN_STRN(buf, (int)strlen(buf)))),
                                 potion_source(P, AST_MSG, PN_STR("die"), PN_NIL, PN_NIL, lineno, line)),
                         PN_NIL, PN_NIL, lineno, line);
  }
  int sp = nm && (!strcmp(nm, "shift") || !strcmp(nm, "pop"));
  return potion_source(P, AST_EXPR,
                       PN_PUSH(PN_TUP(potion_source(P, AST_MSG, PN_STR(sp ? "@_" : "$_"), PN_NIL, PN_NIL, lineno, line)), u),
                       PN_NIL, PN_NIL, lineno, line);
}
/* the condition of `while (COND) BLOCK` / `if (COND) BLOCK` / `UNTIL` is in scalar context:
 * a one-item list: @a is its length, each %h its key, keys %h the count (p5_cond).
 * Only for while/if/elseif — foreach/for use the list as the iterated list.
 * greg note: single-letter rule symbols (a, m, l, ...) are #defined in the generated action
 * blocks, so macros that use `->a` (PN_S) must not be expanded there — keep such checks in
 * the top-of-file C section. */
static PN p5_special_cond(Potion *P, long lineno, PN line, PN m, PN l) {
  if (!(PN_IS_PTR(m) && PN_VTYPE(m) == PN_TSOURCE && PN_PART(m) == AST_MSG && PN_IS_STR(PN_S(m, 0))
        && (!strcmp(PN_STR_PTR(PN_S(m, 0)), "while") || !strcmp(PN_STR_PTR(PN_S(m, 0)), "if") || !strcmp(PN_STR_PTR(PN_S(m, 0)), "elseif"))))
    return l;
  if (PN_IS_PTR(l) && PN_VTYPE(l) == PN_TSOURCE && PN_PART(l) == AST_LIST
      && PN_IS_TUPLE(PN_S(l, 0)) && PN_TUPLE_LEN(PN_S(l, 0)) == 1) {
    PN it = p5_cond(P, lineno, line, PN_TUPLE_AT(PN_S(l, 0), 0));
    if (it != PN_TUPLE_AT(PN_S(l, 0), 0)) { PN_TUPLE_AT(PN_S(l, 0), 0) = it; PN_TOUCH(l); }
  }
  return l;
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
static PN p5_flatten_multi(Potion *P, long lineno, PN line, PN list_ast);
static PN p5_unparen(Potion *P, PN e);
static int p5_arrayish(Potion *P, PN item);
static PN p5_push(Potion *P, long lineno, PN line, PN op, PN arr, PN items) {
  PN out;
  { /* push @$x, ...: an undef scalar $x springs to life as an array reference */
    PN a0 = PN_IS_PTR(arr) && PN_TYPE(arr) == PN_TSOURCE && PN_SRC(arr)->part == AST_EXPR ? (PN)PN_SRC(arr)->a[0] : PN_NIL;
    if (PN_IS_TUPLE(a0) && PN_TUPLE_LEN(a0) == 2) {
      PN x = PN_TUPLE_AT(a0, 0), m = PN_TUPLE_AT(a0, 1);
      PN xm = PN_IS_PTR(x) && PN_TYPE(x) == PN_TSOURCE && PN_SRC(x)->part == AST_EXPR && PN_IS_TUPLE((PN)PN_SRC(x)->a[0]) &&
              PN_TUPLE_LEN((PN)PN_SRC(x)->a[0]) == 1 ? PN_TUPLE_AT((PN)PN_SRC(x)->a[0], 0) : x;
      if (PN_IS_PTR(m) && PN_TYPE(m) == PN_TSOURCE && PN_SRC(m)->part == AST_MSG && PN_IS_STR((PN)PN_SRC(m)->a[0]) &&
          !strcmp(PN_STR_PTR((PN)PN_SRC(m)->a[0]), "p5aref") &&
          PN_IS_PTR(xm) && PN_TYPE(xm) == PN_TSOURCE && PN_SRC(xm)->part == AST_MSG && PN_IS_STR((PN)PN_SRC(xm)->a[0]) &&
          PN_STR_LEN((PN)PN_SRC(xm)->a[0]) > 1 && PN_STR_PTR((PN)PN_SRC(xm)->a[0])[0] == '$' && (PN)PN_SRC(xm)->a[1] == PN_NIL) {
        PN viv = potion_source(P, AST_EXPR, PN_PUSH(PN_TUP(x), potion_source(P, AST_MSG, PN_STR("p5viv"), PN_NIL, PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line);
        arr = potion_source(P, AST_EXPR, PN_TUP(potion_source(P, AST_ASSIGN, x, viv, PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line);
      }
    }
  }
  out = PN_TUP(arr);
  if (PN_TUPLE_LEN(items) > 0) {
    /* push @a, 1, @b, (): the flattened list is added by one native (p5pushall / p5unshiftall) */
    PN lst = potion_source(P, AST_LIST, items, PN_NIL, PN_NIL, lineno, line);
    PN flat;
    if (PN_TUPLE_LEN(items) == 1) { /* the item itself when it is a list (@b, (1,2), qw()), else a one element list */
      PN it = p5_unparen(P, PN_TUPLE_AT(items, 0));
      flat = p5_arrayish(P, it) ? it : potion_source(P, AST_LIST, PN_TUP(it), PN_NIL, PN_NIL, lineno, line);
    } else flat = p5_flatten_multi(P, lineno, line, lst);
    PN args = potion_source(P, AST_LIST, PN_TUP(potion_source(P, AST_EXPR, PN_TUP(flat), PN_NIL, PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line);
    out = PN_PUSH(out, potion_source(P, AST_MSG, PN_STR(PN_STR_PTR(op)[0] == 'u' ? "p5unshiftall" : "p5pushall"), args, PN_NIL, lineno, line));
  }
  out = PN_PUSH(out, potion_source(P, AST_MSG, PN_STR("length"), PN_NIL, PN_NIL, lineno, line)); /* the value is the new number of elements */
  return potion_source(P, AST_EXPR, out, PN_NIL, PN_NIL, lineno, line);
}

/* splice(@a, OFF, LEN, LIST): T is (LEN, LIST-or-nil, nil) */
static PN p5_splice_ast(Potion *P, long lineno, PN line, PN arr, PN off, PN t) {
  PN items = PN_TUPLE_LEN(t) > 1 ? PN_TUPLE_AT(t, 1) : potion_source(P, AST_VALUE, PN_NIL, PN_NIL, PN_NIL, lineno, line);
  PN args = PN_PUSH(PN_PUSH(PN_TUP(p5_unparen(P, off)), PN_TUPLE_AT(t, 0)), items);
  return potion_source(P, AST_EXPR, PN_PUSH(PN_TUP(arr), potion_source(P, AST_MSG, PN_STR("p5splice"),
           potion_source(P, AST_LIST, args, PN_NIL, PN_NIL, lineno, line), PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line);
}
/* substr($s, OFF [, LEN]) = R and substr($s, OFF, LEN, R): $s = $s->p5substrset(OFF, LEN, R) */
static PN p5_substr_set(Potion *P, long lineno, PN line, PN var, PN off, PN len, PN repl) {
  PN call = potion_source(P, AST_EXPR, PN_PUSH(PN_TUP(var), potion_source(P, AST_MSG, PN_STR("p5substrset"),
              potion_source(P, AST_LIST, PN_PUSH(PN_PUSH(PN_TUP(p5_unparen(P, off)), len), p5_unparen(P, repl)), PN_NIL, PN_NIL, lineno, line),
              PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line);
  return potion_source(P, AST_EXPR, PN_TUP(potion_source(P, AST_ASSIGN, var, call, PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line);
}
/* {k => v, ...} anonymous hash: LIST -> table */
static PN p5_flatten_multi(Potion *P, long lineno, PN line, PN list_ast);
static PN p5_anonhash(Potion *P, long lineno, PN line, PN items) {
  PN lst = potion_source(P, AST_LIST, items, PN_NIL, PN_NIL, lineno, line);
  { PN f = p5_flatten_multi(P, lineno, line, lst); /* { n => 0, @_ }: array items are spread */
    if (f != lst) return potion_source(P, AST_EXPR, PN_PUSH(PN_TUP(f), potion_source(P, AST_MSG, PN_STR("table"), PN_NIL, PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line); }
  PN msg = potion_source(P, AST_MSG, PN_STR("table"), PN_NIL, PN_NIL, lineno, line);
  return potion_source(P, AST_EXPR, PN_PUSH(PN_TUP(lst), msg),
                       PN_NIL, PN_NIL, lineno, line);
}

/* '(1..5)' and '(@b)' on the rhs of an array assignment: the one item is
 * itself a list, so use it directly instead of nesting it in a one-element
 * tuple (see also the same special case in p5 foreach). */
/* does the list item evaluate to a list that a surrounding list literal flattens?
 * (@a, @$r, @{...}, slices, ranges, map/grep/sort, keys/values/reverse/split) */
static PN p5_pat_array, p5_pat_array_bare;
static int p5_arrayish(Potion *P, PN item) {
  PN n = item, st, m, caps[PN_AST_CAPS];
  if (!p5_pat_array) { /* LIST | MSG(@* | %*, no args) | MSG(list op name, any args) */
    PN sigil = potion_ast_alt(P, PN_PUSH(PN_PUSH(PN_TUP0(), PN_STR("@*")), PN_STR("%*")));
    PN names = PN_TUP0();
    static const char *ops[] = { "p5aref", "p5slice", "p5sliceof", "p5matchall", "p5matchl", "p5range", "p5map", "p5grep", "p5sort", "keys",
                                 "values", "reverse", "split", "p5each", "p5lrepeat", "p5splice", "glob", 0 };
    int k;
    for (k = 0; ops[k]; k++) names = PN_PUSH(names, PN_STR(ops[k]));
    p5_pat_array_bare = potion_source(P, AST_MSG, sigil, PN_NIL, potion_ast_wild(P, -1), 0, PN_NIL); potion_gc_root(P, &p5_pat_array_bare);
    p5_pat_array = potion_ast_alt(P, PN_PUSH(PN_PUSH(PN_PUSH(PN_TUP0(),
      potion_source(P, AST_LIST, potion_ast_wild(P, -1), potion_ast_wild(P, -1), potion_ast_wild(P, -1), 0, PN_NIL)),
      p5_pat_array_bare),
      potion_source(P, AST_MSG, potion_ast_alt(P, names), potion_ast_wild(P, -1), potion_ast_wild(P, -1), 0, PN_NIL)));
    potion_gc_root(P, &p5_pat_array);
  }
  if (potion_ast_match(P, p5_pat_array_bare, item, caps, 0)) return 1; /* a bare @a / %h item */
  for (;;) { /* the last item of EXPR[EXPR[..]] chains is the one that counts */
    if (!PN_IS_PTR(n) || PN_VTYPE(n) != PN_TSOURCE || PN_PART(n) != AST_EXPR) return 0;
    st = PN_S(n, 0);
    if (!PN_IS_TUPLE(st) || PN_TUPLE_LEN(st) == 0) return 0;
    m = PN_TUPLE_AT(st, PN_TUPLE_LEN(st) - 1);
    if (PN_TUPLE_LEN(st) == 1 && PN_IS_PTR(m) && PN_VTYPE(m) == PN_TSOURCE && PN_PART(m) == AST_EXPR) { n = m; continue; }
    if (PN_IS_PTR(m) && PN_VTYPE(m) == PN_TSOURCE && PN_PART(m) == AST_LIST && PN_TUPLE_LEN(st) != 1) return 0; /* (1, 2) is a list only alone */
    return potion_ast_match(P, p5_pat_array, m, caps, 0);
  }
}
/* (@b) x 2: the list literal around a lone array is that array */
static PN p5_listlit_array(Potion *P, PN p) {
  PN l = p;
  if (PN_IS_PTR(l) && PN_VTYPE(l) == PN_TSOURCE && PN_PART(l) == AST_EXPR && PN_IS_TUPLE(PN_S(l, 0)) && PN_TUPLE_LEN(PN_S(l, 0)) == 1) l = PN_TUPLE_AT(PN_S(l, 0), 0);
  if (PN_IS_PTR(l) && PN_VTYPE(l) == PN_TSOURCE && PN_PART(l) == AST_LIST && PN_IS_TUPLE(PN_S(l, 0)) && PN_TUPLE_LEN(PN_S(l, 0)) == 1 &&
      p5_arrayish(P, PN_TUPLE_AT(PN_S(l, 0), 0)) && !p5_is_listlit(P, PN_TUPLE_AT(PN_S(l, 0), 0)))
    return PN_TUPLE_AT(PN_S(l, 0), 0);
  return p;
}
/* (0, @a, 3) => [0].p5ext(@a).p5ext([3]): consecutive scalar items stay one run */
/* ((map ...), 7): a parenthesized list holding one list valued item is that item itself (not a nested array) */
static PN p5_unwrap_list1(Potion *P, PN it) {
  PN n = it, l, x;
  while (PN_IS_PTR(n) && PN_VTYPE(n) == PN_TSOURCE && PN_PART(n) == AST_EXPR && PN_IS_TUPLE(PN_S(n, 0)) && PN_TUPLE_LEN(PN_S(n, 0)) == 1) n = PN_TUPLE_AT(PN_S(n, 0), 0);
  if (!(PN_IS_PTR(n) && PN_VTYPE(n) == PN_TSOURCE && PN_PART(n) == AST_LIST && PN_IS_TUPLE(PN_S(n, 0)) && PN_TUPLE_LEN(PN_S(n, 0)) == 1)) return it;
  l = PN_TUPLE_AT(PN_S(n, 0), 0);
  x = l;
  while (PN_IS_PTR(x) && PN_VTYPE(x) == PN_TSOURCE && PN_PART(x) == AST_EXPR && PN_IS_TUPLE(PN_S(x, 0)) && PN_TUPLE_LEN(PN_S(x, 0)) == 1) x = PN_TUPLE_AT(PN_S(x, 0), 0);
  if (PN_IS_PTR(x) && PN_VTYPE(x) == PN_TSOURCE && PN_PART(x) == AST_LIST) return it; /* ((1, 2)): handled as a literal */
  return p5_arrayish(P, l) && !p5_is_listlit(P, l) ? l : it;
}
static PN p5_flatten_multi(Potion *P, long lineno, PN line, PN list_ast) {
  PN items = PN_S(list_ast, 0), acc = PN_NIL, run = PN_TUP0();
  PN_SIZE i;
  int any = 0;
  if (!PN_IS_TUPLE(items) || PN_TUPLE_LEN(items) < 2) return list_ast;
  for (i = 0; i < PN_TUPLE_LEN(items); i++) { p5_listctx(P, PN_TUPLE_AT(items, i)); if (p5_arrayish(P, PN_TUPLE_AT(items, i))) any = 1; }
  if (!any) return list_ast;
#define P5_EXT(a, other) \
  potion_source(P, AST_EXPR, PN_PUSH(PN_TUP(a), potion_source(P, AST_MSG, PN_STR("p5ext"), \
    potion_source(P, AST_LIST, PN_TUP(other), PN_NIL, PN_NIL, lineno, line), PN_NIL, lineno, line)), \
    PN_NIL, PN_NIL, lineno, line)
#define P5_RUNLIST(r) potion_source(P, AST_LIST, r, PN_NIL, PN_NIL, lineno, line)
  for (i = 0; i < PN_TUPLE_LEN(items); i++) {
    PN it = p5_unwrap_list1(P, PN_TUPLE_AT(items, i));
    if (p5_arrayish(P, it)) {
      if (PN_TUPLE_LEN(run) > 0) {
        acc = (acc == PN_NIL) ? P5_RUNLIST(run)
                              : P5_EXT(acc, potion_source(P, AST_EXPR, PN_TUP(P5_RUNLIST(run)), PN_NIL, PN_NIL, lineno, line));
        run = PN_TUP0();
      }
      acc = (acc == PN_NIL) ? P5_EXT(potion_source(P, AST_LIST, PN_TUP0(), PN_NIL, PN_NIL, lineno, line), it) : P5_EXT(acc, it);
    } else run = PN_PUSH(run, it);
  }
  if (PN_TUPLE_LEN(run) > 0)
    acc = P5_EXT(acc, potion_source(P, AST_EXPR, PN_TUP(P5_RUNLIST(run)), PN_NIL, PN_NIL, lineno, line));
#undef P5_EXT
#undef P5_RUNLIST
  return potion_source(P, AST_EXPR, PN_TUP(acc), PN_NIL, PN_NIL, lineno, line); /* the list itself, not a LIST ast */
}

/* @b = @a copies: a plain array variable on the right is cloned (arrays are the tuples themselves) */
static int p5_is_arrvar(PN n) {
  if (PN_IS_PTR(n) && PN_VTYPE(n) == PN_TSOURCE && PN_PART(n) == AST_EXPR && PN_IS_TUPLE(PN_S(n, 0)) && PN_TUPLE_LEN(PN_S(n, 0)) == 1)
    n = PN_TUPLE_AT(PN_S(n, 0), 0);
  return PN_IS_PTR(n) && PN_VTYPE(n) == PN_TSOURCE && PN_PART(n) == AST_MSG && PN_IS_STR(PN_S(n, 0)) &&
         PN_STR_LEN(PN_S(n, 0)) > 1 && PN_STR_PTR(PN_S(n, 0))[0] == '@' && PN_S(n, 1) == PN_NIL;
}
/* lhs = rhs where both are plain array variables: the rhs is cloned */
static PN p5_copy_rhs(Potion *P, PN lhs, PN rhs) {
  if (p5_is_arrvar(lhs) && p5_is_arrvar(rhs))
    return potion_source(P, AST_EXPR, PN_PUSH(PN_TUP(rhs), potion_source(P, AST_MSG, PN_STR("clone"), PN_NIL, PN_NIL, 0, PN_NIL)), PN_NIL, PN_NIL, 0, PN_NIL);
  return rhs;
}
static PN p5_readline_all(Potion *P, PN item);
static PN p5_flatten1(Potion *P, PN list_ast) {
  PN items = PN_S(list_ast, 0), only;
  if (!PN_IS_TUPLE(items) || PN_TUPLE_LEN(items) != 1) return list_ast;
  { PN all = p5_readline_all(P, PN_TUPLE_AT(items, 0)); /* @a = <FH>: all the records */
    if (all != PN_NIL) return all; }
  only = p5_listctx(P, PN_TUPLE_AT(items, 0));
  if (p5_arrayish(P, only)) return only;
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
      PN el;
      if (PN_STR_PTR(PN_S(m, 0))[1] == '$') { /* @$ref: $ref->[i] */
        PN rv = potion_source(P, AST_MSG, potion_str(P, PN_STR_PTR(PN_S(m, 0)) + 1),
                              PN_NIL, PN_NIL, lineno, line);
        el = potion_source(P, AST_MSG, PN_STR("at"),
               potion_source(P, AST_LIST, PN_TUP(idx), PN_NIL, PN_NIL, lineno, line),
               PN_NIL, lineno, line);
        return potion_source(P, AST_EXPR, PN_PUSH(PN_TUP(rv), el), PN_NIL, PN_NIL, lineno, line);
      }
      el = potion_source(P, AST_MSG, PN_S(m, 0),
                potion_source(P, AST_LIST, PN_TUP(idx), PN_NIL, PN_NIL, lineno, line),
                PN_NIL, lineno, line);
      return potion_source(P, AST_EXPR, PN_TUP(el), PN_NIL, PN_NIL, lineno, line);
    }
  }
  if (!PN_IS_TUPLE(items) || i >= (long)PN_TUPLE_LEN(items))
    return potion_source(P, AST_VALUE, PN_NIL, PN_NIL, PN_NIL, lineno, line); /* undef */
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
static PN p5_regexp1(Potion *P, long lineno, char *literal, long len) {
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
  /* /$re/, /[$class]/: a '$name' (not '$)' '$|' or '\\$') interpolates the variable */
  {
    long i, n = close - 1, start = 0;
    int interp = 0;
    for (i = 0; i < n; i++) {
      if (literal[1 + i] == '\\') { i++; continue; }
      if (literal[1 + i] == '$' && i + 1 < n &&
          (isalpha((unsigned char)literal[2 + i]) || literal[2 + i] == '_' || (literal[2 + i] == '{' && i + 2 < n && isalpha((unsigned char)literal[3 + i])))) { interp = 1; break; }
    }
    if (interp) {
      PN acc = PN_NIL;
      char pre[8]; int np = 0;
      if (caseless || multiline || dotall || extended) {
        pre[np++] = '('; pre[np++] = '?';
        if (caseless) pre[np++] = 'i';
        if (multiline) pre[np++] = 'm';
        if (dotall) pre[np++] = 's';
        if (extended) pre[np++] = 'x';
        pre[np++] = ')';
      }
      acc = potion_source(P, AST_VALUE, PN_STRN(pre, np), PN_NIL, PN_NIL, lineno, P->line);
      for (i = 0; i < n; i++) {
        if (literal[1 + i] == '\\') { i++; continue; }
        if (literal[1 + i] == '$' && i + 1 < n &&
            (isalpha((unsigned char)literal[2 + i]) || literal[2 + i] == '_' || (literal[2 + i] == '{' && i + 2 < n && isalpha((unsigned char)literal[3 + i])))) {
          long j = i + 1;
          char nm[80]; int k = 0;
          int brace = literal[1 + j] == '{';
          if (brace) j++;
          while (j < n && (isalnum((unsigned char)literal[1 + j]) || literal[1 + j] == '_') && k < 70)
            nm[k++] = literal[1 + j++];
          if (brace && j < n && literal[1 + j] == '}') j++;
          acc = p5_cat2(P, lineno, P->line, acc,
                  potion_source(P, AST_VALUE, PN_STRN(literal + 1 + start, i - start), PN_NIL, PN_NIL, lineno, P->line));
          nm[k] = 0;
          acc = p5_cat2(P, lineno, P->line, acc,
                  potion_source(P, AST_MSG, PN_STRCAT("$", nm), PN_NIL, PN_NIL, lineno, P->line));
          start = j; i = j - 1;
        }
      }
      return p5_cat2(P, lineno, P->line, acc,
               potion_source(P, AST_VALUE, PN_STRN(literal + 1 + start, n - start), PN_NIL, PN_NIL, lineno, P->line));
    }
  }
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

/* /g and /c of the regex literal parsed last: not part of the pattern, the callers choose the match */
static int p5_re_g, p5_re_c;
/* LITERAL is "/pat/flags": the flags g and c are removed and noted in p5_re_g / p5_re_c */
static PN p5_regexp(Potion *P, long lineno, char *literal, long len) {
  char buf[len + 1];
  long close = len - 1, n = 0, i;
  while (close > 0 && literal[close] != '/') close--;
  memcpy(buf, literal, close + 1); n = close + 1;
  p5_re_g = p5_re_c = 0;
  for (i = close + 1; i < len; i++) {
    if (literal[i] == 'g') p5_re_g = 1;
    else if (literal[i] == 'c') p5_re_c = 1;
    else buf[n++] = literal[i];
  }
  return p5_regexp1(P, lineno, buf, n);
}
/* m{PAT}flags, s!PAT!REPL!, qr|PAT|: the body without its delimiters as the literal /PAT/flags */
static PN p5_re_lit(Potion *P, long lineno, PN body, PN flags) {
  size_t bl = PN_STR_LEN(body), fl = PN_STR_LEN(flags), i, n = 0;
  char *buf = malloc(bl * 2 + fl + 3);
  PN r;
  if (!buf) potion_allocation_error();
  buf[n++] = '/';
  for (i = 0; i < bl; i++) {
    char c = PN_STR_PTR(body)[i];
    if (c == '\\' && i + 1 < bl) { buf[n++] = c; buf[n++] = PN_STR_PTR(body)[++i]; }
    else { if (c == '/') buf[n++] = '\\'; buf[n++] = c; }
  }
  buf[n++] = '/';
  memcpy(buf + n, PN_STR_PTR(flags), fl); n += fl;
  r = p5_regexp(P, lineno, buf, (long)n);
  free(buf);
  return r;
}

/* m?pat?: like m/pat/ but matches only once until reset (perl's PMf_ONCE).
 * The pattern text carries a \001<id>; marker prefix, unique per textual
 * occurrence (perl's once state is per op, not per pattern text);
 * p5match/p5matchl enforce the once state and p5reset clears it. */
static long p5_once_seq;
static PN p5_re_once(Potion *P, long lineno, PN body, PN flags) {
  char pre[24];
  int n = snprintf(pre, sizeof(pre), "\001%ld;", p5_once_seq++);
  PN marked = PN_STRN(pre, n);
  marked = potion_strcat(P, PN_STR_PTR(marked), PN_STR_PTR(body));
  return p5_re_lit(P, lineno, marked, flags);
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
/* if (IDX > 0) { @ARR(IDX - 1) = $var } : the element the loop variable was bound to (the index is already advanced) */
static PN p5_for_writeback(Potion *P, long lineno, PN line, PN arr, PN idxname, PN var) {
  PN idx1 = potion_source(P, AST_MSG, idxname, PN_NIL, PN_NIL, lineno, line);
  PN idx2 = potion_source(P, AST_MSG, idxname, PN_NIL, PN_NIL, lineno, line);
  PN zero = potion_source(P, AST_VALUE, PN_ZERO, PN_NIL, PN_NIL, lineno, line);
  PN one = potion_source(P, AST_VALUE, PN_NUM(1), PN_NIL, PN_NIL, lineno, line);
  PN cond = potion_source(P, AST_GT, idx1, zero, PN_NIL, lineno, line);
  PN elem = potion_source(P, AST_MSG, arr, potion_source(P, AST_LIST, PN_TUP(potion_source(P, AST_MINUS, idx2, one, PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line), PN_NIL, lineno, line);
  PN val = potion_source(P, AST_MSG, PN_S(var, 0), PN_NIL, PN_NIL, lineno, line);
  PN asg = potion_source(P, AST_EXPR, PN_TUP(potion_source(P, AST_ASSIGN, elem, val, PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line);
  return potion_source(P, AST_EXPR, PN_TUP(potion_source(P, AST_MSG, PN_if,
           potion_source(P, AST_LIST, PN_TUP(cond), PN_NIL, PN_NIL, lineno, line),
           potion_source(P, AST_BLOCK, PN_TUP(asg), PN_NIL, PN_NIL, lineno, line), lineno, line)), PN_NIL, PN_NIL, lineno, line);
}
static int p5_lex_my; /* the last foreach variable had a `my` */
/* is the loop variable a global (a plain `$name`, not `$_`, no `my`)? */
static int p5_lex_my_global(PN loopvar) {
  return !p5_lex_my && PN_IS_PTR(loopvar) && PN_PART(loopvar) == AST_MSG && PN_IS_STR(PN_S(loopvar, 0)) &&
         PN_STR_PTR(PN_S(loopvar, 0))[0] == '$' && strcmp(PN_STR_PTR(PN_S(loopvar, 0)), "$_") &&
         !strstr(PN_STR_PTR(PN_S(loopvar, 0)), "__for_");
}
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
  if (PN_IS_TUPLE(items0)) { PN_SIZE fi; for (fi = 0; fi < PN_TUPLE_LEN(items0); fi++) p5_listctx(P, PN_TUPLE_AT(items0, fi)); } /* the list of a foreach is in list context */
  if (PN_IS_TUPLE(items0) && PN_TUPLE_LEN(items0) == 1) {
    PN only = potion_tuple_at(P, 0, items0, PN_NUM(0));
    if (PN_PART(only) == AST_EXPR && PN_TUPLE_LEN(PN_S(only,0)) == 1)
      only = potion_tuple_at(P, 0, PN_S(only,0), PN_NUM(0));
    if (p5_arrayish(P, only) && PN_PART(only) == AST_EXPR) for_rhs = only; /* @$r, slices, keys ...: the list itself */
    else if (PN_PART(only) == AST_MSG) {
      PN nm = PN_S(only, 0);
      if (PN_STR_LEN(nm) > 0 && PN_STR_PTR(nm)[0] == '@') for_rhs = only;
      else if (PN_STR_LEN(nm) == 7 && !memcmp(PN_STR_PTR(nm), "p5range", 7))
        for_rhs = potion_tuple_at(P, 0, items0, PN_NUM(0));
      else if (p5_arrayish(P, PN_TUPLE_AT(items0, 0))) for_rhs = only; /* split(..), reverse(..): the list itself */
      else if (p5_is_callish(PN_TUPLE_AT(items0, 0))) /* foreach (f()): a list result is iterated, a scalar is one element */
        for_rhs = potion_source(P, AST_EXPR, PN_TUP(potion_source(P, AST_MSG, PN_STR("p5aslist"),
                    potion_source(P, AST_LIST, PN_TUP(PN_TUPLE_AT(items0, 0)), PN_NIL, PN_NIL, lineno, line), PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line);
    } else if (PN_PART(only) == AST_LIST) {
      for_rhs = only;
    }
  }

  if (for_rhs == list_ast && PN_IS_TUPLE(items0) && PN_TUPLE_LEN(items0) > 1) { /* for ("a", qw(b c)): the array like items are spread */
    PN_SIZE fi; int any = 0;
    for (fi = 0; fi < PN_TUPLE_LEN(items0); fi++) if (p5_arrayish(P, PN_TUPLE_AT(items0, fi))) any = 1;
    if (any) for_rhs = p5_flatten_multi(P, lineno, line, list_ast);
  }
  /* the loop variable aliases the elements of a plain array: it is written back (p5_for_writeback) */
  PN alias_arr = PN_NIL;
  if (for_rhs != list_ast && PN_IS_PTR(for_rhs) && PN_PART(for_rhs) == AST_MSG && PN_IS_STR(PN_S(for_rhs, 0)) &&
      PN_STR_PTR(PN_S(for_rhs, 0))[0] == '@' && PN_S(for_rhs, 1) == PN_NIL)
    alias_arr = PN_S(for_rhs, 0);
  if (alias_arr != PN_NIL && p5_tied_name(alias_arr)) { /* foreach over a tied array: its elements, no write back */
    for_rhs = p5_fcall(P, lineno, line, "p5tlist", PN_TUP(for_rhs));
    alias_arr = PN_NIL;
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
  if (alias_arr != PN_NIL && !p5_lex_my_global(loopvar)) PN_SRC(stmt_bind)->a[2] = PN_SRC(PN_NUM(1)); /* compile.c: a fresh variable per iteration although the name exists */

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
  if (alias_arr != PN_NIL && PN_PART(loopvar) == AST_MSG) newstmts = PN_PUSH(newstmts, p5_for_writeback(P, lineno, line, alias_arr, idxname, loopvar));
  newstmts = PN_PUSH(newstmts, stmt_bind);
  newstmts = PN_PUSH(newstmts, stmt_incr);
  { PN origstmts = PN_S(body_block, 0);
    if (PN_IS_TUPLE(origstmts))
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
  /* for $n (..) with a global $n (no `my`): perl saves the global and restores it when the loop ends, closures
   * read the global. Here: $__for_sv_N = $n; $n = $__for_sv_N (the cell exists before the loop, so the body only
   * assigns it); the loop; $n = $__for_sv_N. */
  int global_var = !p5_lex_my && PN_IS_PTR(loopvar) && PN_PART(loopvar) == AST_MSG && PN_IS_STR(PN_S(loopvar, 0)) &&
                   PN_STR_PTR(PN_S(loopvar, 0))[0] == '$' && strcmp(PN_STR_PTR(PN_S(loopvar, 0)), "$_") &&
                   !strstr(PN_STR_PTR(PN_S(loopvar, 0)), "__for_");
  char svbuf[40];
  int svn = snprintf(svbuf, sizeof svbuf, "$__for_sv_%d", n);
  PN svname = PN_STRN(svbuf, svn);
  if (global_var) {
    PN lv1 = potion_source(P, AST_MSG, PN_S(loopvar, 0), PN_NIL, PN_NIL, lineno, line);
    PN lv2 = potion_source(P, AST_MSG, PN_S(loopvar, 0), PN_NIL, PN_NIL, lineno, line);
    PN sv1 = potion_source(P, AST_MSG, svname, PN_NIL, PN_NIL, lineno, line);
    PN sv2 = potion_source(P, AST_MSG, svname, PN_NIL, PN_NIL, lineno, line);
    stmts = PN_PUSH(stmts, potion_source(P, AST_EXPR, PN_TUP(potion_source(P, AST_ASSIGN, sv1, lv1, PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line));
    stmts = PN_PUSH(stmts, potion_source(P, AST_EXPR, PN_TUP(potion_source(P, AST_ASSIGN, lv2, sv2, PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line));
  }
  if (alias_arr != PN_NIL && !global_var && PN_PART(loopvar) == AST_MSG) /* the write-back reads the variable before the bind: it must exist as a local */
    stmts = PN_PUSH(stmts, potion_source(P, AST_EXPR, PN_TUP(potion_source(P, AST_ASSIGN,
              potion_source(P, AST_MSG, PN_S(loopvar, 0), PN_NIL, PN_NIL, lineno, line),
              potion_source(P, AST_VALUE, PN_NIL, PN_NIL, PN_NIL, lineno, line), PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line));
  stmts = PN_PUSH(stmts, stmt_arr);
  stmts = PN_PUSH(stmts, stmt_idx);
  stmts = PN_PUSH(stmts, potion_source(P, AST_EXPR, PN_TUP(whilemsg), PN_NIL, PN_NIL, lineno, line));
  if (alias_arr != PN_NIL && PN_PART(loopvar) == AST_MSG) stmts = PN_PUSH(stmts, p5_for_writeback(P, lineno, line, alias_arr, idxname, loopvar));
  if (global_var) {
    PN lv3 = potion_source(P, AST_MSG, PN_S(loopvar, 0), PN_NIL, PN_NIL, lineno, line);
    PN sv3 = potion_source(P, AST_MSG, svname, PN_NIL, PN_NIL, lineno, line);
    stmts = PN_PUSH(stmts, potion_source(P, AST_EXPR, PN_TUP(potion_source(P, AST_ASSIGN, lv3, sv3, PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line));
  }
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
  PN m = potion_source(P, AST_MSG, PN_STR("p5str"), PN_NIL, PN_NIL, lineno, line); /* p5_tostr: overloaded "" */
  return potion_source(P, AST_EXPR, PN_PUSH(PN_TUP(v), m), PN_NIL, PN_NIL, lineno, line);
}
/* a plain variable or constant: safe to evaluate twice */
static int p5_simple_operand(PN e) {
  PN st, m;
  if (PN_PART(e) == AST_VALUE) return 1;
  if (PN_PART(e) != AST_EXPR) return 0;
  st = PN_S(e, 0);
  if (!PN_IS_TUPLE(st) || PN_TUPLE_LEN(st) != 1) return 0;
  m = PN_TUPLE_AT(st, 0);
  if (PN_PART(m) == AST_VALUE) return 1;
  return PN_PART(m) == AST_MSG && PN_S(m, 1) == PN_NIL && PN_IS_STR(PN_S(m, 0));
}
/* A == B: the native AST_EQ compares bit patterns, which is right for two ints
 * and for references but wrong for "6" == 6 or two equal doubles. When both
 * operands are simple, fall back to p5numeq (numeric compare of numbers and
 * numeric strings) if the fast test fails. */
/* a comparison result is the boolean value: numify it so that '(a > b) == 1' holds */
static PN p5_boolnum(Potion *P, long lineno, PN line, PN e) {
  int part = PN_IS_PTR(e) && PN_VTYPE(e) == PN_TSOURCE ? (int)PN_PART(e) : -1;
  if (part == AST_NOT || (part >= AST_EQ && part <= AST_LTE))
    return potion_source(P, AST_PLUS, e, potion_source(P, AST_VALUE, PN_ZERO, PN_NIL, PN_NIL, lineno, line),
                         PN_NIL, lineno, line);
  return e;
}
static PN p5_numeq(Potion *P, long lineno, PN line, PN a, PN b) {
  PN eq;
  a = p5_boolnum(P, lineno, line, a); b = p5_boolnum(P, lineno, line, b);
  eq = potion_source(P, AST_EQ, a, b, PN_NIL, lineno, line);
  PN call;
  int simple = p5_simple_operand(a) && p5_simple_operand(b);
  if (PN_PART(a) == AST_ASSIGN || PN_PART(b) == AST_ASSIGN) return eq; /* an assignment as a call argument is lost */
  call = potion_source(P, AST_EXPR, PN_TUP(potion_source(P, AST_MSG, PN_STR("p5numeq"),
           potion_source(P, AST_LIST, PN_PUSH(PN_TUP(a), b), PN_NIL, PN_NIL, lineno, line),
           PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line);
  (void)simple; (void)eq; /* no identity shortcut: two identical strings "NaN" are not equal */
  return call;
}
/* A lt B, gt, le, ge: (A cmp B) OP 0 on the string values */
static PN p5_strcmp(Potion *P, long lineno, PN line, int op, PN a, PN b) {
  PN c = potion_source(P, AST_CMP, p5_strval(P, lineno, line, a), p5_strval(P, lineno, line, b), PN_NIL, lineno, line);
  return potion_source(P, op, c, potion_source(P, AST_VALUE, PN_ZERO, PN_NIL, PN_NIL, lineno, line), PN_NIL, lineno, line);
}
/* $a["1foo"]: a string literal index is numified at parse time (perl: 1) */
static PN p5_index(Potion *P, PN i) {
  while (PN_IS_PTR(i) && PN_VTYPE(i) == PN_TSOURCE && PN_PART(i) == AST_EXPR && PN_IS_TUPLE(PN_S(i, 0)) && PN_TUPLE_LEN(PN_S(i, 0)) == 1)
    i = PN_TUPLE_AT(PN_S(i, 0), 0);
  if (PN_IS_PTR(i) && PN_VTYPE(i) == PN_TSOURCE && PN_PART(i) == AST_VALUE && PN_IS_STR(PN_S(i, 0))) {
    char *p = PN_STR_PTR(PN_S(i, 0));
    return potion_source(P, AST_VALUE, PN_NUM(strtoll(p, NULL, 10)), PN_NIL, PN_NIL, 0, PN_NIL);
  }
  return i;
}
/* the elements of [ ITEMS ]: array items (@a, qw(..), a slice, (list)) are spread */
static PN p5_list_items(Potion *P, long lineno, PN line, PN items) {
  PN lst = potion_source(P, AST_LIST, items, PN_NIL, PN_NIL, lineno, line);
  PN f;
  if (PN_IS_TUPLE(items) && PN_TUPLE_LEN(items) == 1 && p5_arrayish(P, PN_TUPLE_AT(items, 0))) return PN_TUPLE_AT(items, 0);
  f = p5_flatten_multi(P, lineno, line, lst);
  return f;
}
/* until (COND): the while condition list (!COND) */
static PN p5_until_cond(Potion *P, long lineno, PN line, PN l) {
  PN c = p5_unparen(P, PN_TUPLE_AT(PN_S(l, 0), 0));
  return potion_source(P, AST_LIST,
           PN_TUP(potion_source(P, AST_NOT, potion_source(P, AST_EXPR, PN_TUPIF(c), PN_NIL, PN_NIL, lineno, line), PN_NIL, PN_NIL, lineno, line)),
           PN_NIL, PN_NIL, lineno, line);
}
/* last LABEL / next LABEL: break/continue with the label as its argument */
static PN p5_loopctl_label(Potion *P, long lineno, PN line, PN kind, PN name) {
  PN args = potion_source(P, AST_LIST, PN_TUP(potion_source(P, AST_VALUE, name, PN_NIL, PN_NIL, lineno, line)),
                          PN_NIL, PN_NIL, lineno, line);
  return potion_source(P, AST_MSG, kind, args, PN_NIL, lineno, line);
}
/* the first (outermost) while in a statement */
static PN p5_find_while(PN n) {
  PN_SIZE i;
  int k;
  if (PN_IS_TUPLE(n)) {
    for (i = 0; i < PN_TUPLE_LEN(n); i++) { PN r = p5_find_while(PN_TUPLE_AT(n, i)); if (r != PN_NIL) return r; }
    return PN_NIL;
  }
  if (!(PN_IS_PTR(n) && PN_VTYPE(n) == PN_TSOURCE)) return PN_NIL;
  if (PN_PART(n) == AST_MSG && PN_S(n, 0) == PN_while) return n;
  for (k = 0; k < 3; k++) { PN r = p5_find_while(PN_S(n, k)); if (r != PN_NIL) return r; }
  return PN_NIL;
}
/* SKIP: { ... skip "why", N if COND; ... }: skip() prints the skipped tests and then leaves the block like
 * `last SKIP`. Every statement of the block whose value is a skip(..) call gets the `last LABEL` after it
 * (nested subs are left alone). Returns the node, or the replacement of it. */
static PN p5_skip_walk(Potion *P, long lineno, PN line, PN n, PN label) {
  int k;
  PN_SIZE i;
  if (PN_IS_TUPLE(n)) {
    for (i = 0; i < PN_TUPLE_LEN(n); i++) {
      PN e = PN_TUPLE_AT(n, i), w = p5_skip_walk(P, lineno, line, e, label);
      if (w != e) { PN_TUPLE_AT(n, i) = w; PN_TOUCH(n); }
    }
    return n;
  }
  if (!(PN_IS_PTR(n) && PN_VTYPE(n) == PN_TSOURCE) || PN_PART(n) == AST_PROTO) return n;
  for (k = 0; k < 3; k++) {
    PN kid = PN_S(n, k), w;
    if (!(PN_IS_TUPLE(kid) || (PN_IS_PTR(kid) && PN_VTYPE(kid) == PN_TSOURCE))) continue;
    w = p5_skip_walk(P, lineno, line, kid, label);
    if (w != kid) { PN_SRC(n)->a[k] = PN_SRC(w); PN_TOUCH(n); }
  }
  if (PN_PART(n) == AST_EXPR && PN_IS_TUPLE(PN_S(n, 0)) && PN_TUPLE_LEN(PN_S(n, 0)) >= 1) {
    PN last = PN_TUPLE_AT(PN_S(n, 0), PN_TUPLE_LEN(PN_S(n, 0)) - 1);
    if (PN_IS_PTR(last) && PN_VTYPE(last) == PN_TSOURCE && PN_PART(last) == AST_MSG && PN_IS_STR(PN_S(last, 0)) &&
        !strcmp(PN_STR_PTR(PN_S(last, 0)), "skip") && PN_S(last, 1) != PN_NIL)
      return potion_source(P, AST_EXPR, PN_PUSH(PN_TUP(n), potion_source(P, AST_EXPR,
               PN_TUP(p5_loopctl_label(P, lineno, line, PN_break, label)), PN_NIL, PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line);
  }
  return n;
}
/* LABEL: loop => the loop body starts with a `__label_LABEL` marker message that the
 * compiler turns into the loop's label (last/next LABEL) */
static PN p5_label_stmt(Potion *P, long lineno, PN line, PN label, PN stmt) {
  PN w = p5_find_while(stmt), body, stmts, marker, ns;
  PN_SIZE i;
  if (w == PN_NIL) return stmt; /* a label on a non loop statement */
  body = PN_S(w, 2);
  if (!(PN_IS_PTR(body) && PN_VTYPE(body) == PN_TSOURCE && PN_IS_TUPLE(PN_S(body, 0)))) return stmt;
  stmts = PN_S(body, 0);
  if (PN_STR_LEN(label) == 4 && !memcmp(PN_STR_PTR(label), "SKIP", 4)) p5_skip_walk(P, lineno, line, stmts, label);
  marker = potion_source(P, AST_EXPR,
             PN_TUP(potion_source(P, AST_MSG, PN_STRCAT("__label_", PN_STR_PTR(label)), PN_NIL, PN_NIL, lineno, line)),
             PN_NIL, PN_NIL, lineno, line);
  ns = PN_TUP(marker);
  for (i = 0; i < PN_TUPLE_LEN(stmts); i++) ns = PN_PUSH(ns, PN_TUPLE_AT(stmts, i));
  PN_SRC(body)->a[0] = PN_SRC(ns); PN_TOUCH(body);
  return stmt;
}
/* The value operated on by `X =~ s///` / `tr///` and the variable it is stored in: for
 * `(my $t = $s) =~ s/a/b/` the source is the assignment expression, the target its lvalue. */
static PN p5_mod_subject(Potion *P, PN subject, PN *target) {
  PN u = p5_unparen(P, subject);
  PN k = u;
  while (PN_IS_PTR(k) && PN_VTYPE(k) == PN_TSOURCE && PN_PART(k) == AST_EXPR && PN_IS_TUPLE(PN_S(k, 0)) && PN_TUPLE_LEN(PN_S(k, 0)) == 1)
    k = PN_TUPLE_AT(PN_S(k, 0), 0);
  if (PN_IS_PTR(k) && PN_VTYPE(k) == PN_TSOURCE && PN_PART(k) == AST_ASSIGN) {
    PN t = PN_S(k, 0);
    while (PN_IS_PTR(t) && PN_VTYPE(t) == PN_TSOURCE && PN_PART(t) == AST_EXPR && PN_IS_TUPLE(PN_S(t, 0)) && PN_TUPLE_LEN(PN_S(t, 0)) == 1)
      t = PN_TUPLE_AT(PN_S(t, 0), 0);
    *target = t;
    return potion_source(P, AST_EXPR, PN_TUP(k), PN_NIL, PN_NIL, 0, PN_NIL);
  }
  *target = u;
  return u;
}
/* `$s =~ s/PAT/REPL/flags`  =>  `($s = $s->subst(/PAT/, "REPL", global))->p5subcount`: the new
 * string is stored, the value of the expression is the number of substitutions. REPL is literal
 * text using PCRE2's $1/${1} group syntax. */
static PN p5_subst(Potion *P, long lineno, PN subject, PN pat, PN repl, int global, PN flags) {
  PN target, source = p5_mod_subject(P, subject, &target);
  PN regex, valrepl, args, msg, call, assign;
  char fl[16];
  size_t i, n = 0;
  for (i = 0; i < PN_STR_LEN(flags) && n < 8; i++) {
    char c = PN_STR_PTR(flags)[i];
    if (c == 'i' || c == 'm' || c == 's' || c == 'x') fl[n++] = c;
  }
  regex = p5_re_lit(P, lineno, pat, PN_STRN(fl, n));
  /* a replacement with variables, escapes or code (/e) is evaluated for every match: a closure */
  {
    int e = memchr(PN_STR_PTR(flags), 'e', PN_STR_LEN(flags)) != NULL;
    const char *r = PN_STR_PTR(repl);
    size_t rl = PN_STR_LEN(repl);
    if (e || memchr(r, '$', rl) || memchr(r, '@', rl) || memchr(r, '\\', rl)) {
      PN code, stmts = PN_NIL, text;
      if (e) text = repl;
      else { /* a double quoted string */
        char *b = malloc(rl * 2 + 3);
        size_t k, o = 0;
        if (!b) potion_allocation_error();
        b[o++] = '"';
        for (k = 0; k < rl; k++) {
          if (r[k] == '\\' && k + 1 < rl) { b[o++] = r[k]; b[o++] = r[++k]; }
          else { if (r[k] == '"') b[o++] = '\\'; b[o++] = r[k]; }
        }
        b[o++] = '"';
        text = potion_str2(P, b, o);
        free(b);
      }
      code = p2_parse(P, text, "<s///>");
      if (PN_IS_PTR(code) && potion_ptr_type(code) == PN_TSOURCE) stmts = PN_S(code, 0);
      if (stmts != PN_NIL) {
        PN proto = p5_sub_proto(P, lineno, P->line, potion_source(P, AST_BLOCK, stmts, PN_NIL, PN_NIL, lineno, P->line));
        args = potion_source(P, AST_LIST,
            PN_PUSH(PN_PUSH(PN_TUP(regex), potion_source(P, AST_EXPR, PN_TUP(proto), PN_NIL, PN_NIL, lineno, P->line)),
                    potion_source(P, AST_VALUE, global ? PN_TRUE : PN_FALSE, PN_NIL, PN_NIL, lineno, P->line)),
            PN_NIL, PN_NIL, lineno, P->line);
        msg = potion_source(P, AST_MSG, PN_STR("p5substf"), args, PN_NIL, lineno, P->line);
        call = potion_source(P, AST_EXPR, PN_PUSH(PN_TUP(source), msg), PN_NIL, PN_NIL, lineno, P->line);
        assign = potion_source(P, AST_ASSIGN, target, call, PN_NIL, lineno, P->line);
        return potion_source(P, AST_EXPR,
                 PN_PUSH(PN_TUP(potion_source(P, AST_EXPR, PN_TUP(assign), PN_NIL, PN_NIL, lineno, P->line)),
                         potion_source(P, AST_MSG, PN_STR("p5subcount"), PN_NIL, PN_NIL, lineno, P->line)),
                 PN_NIL, PN_NIL, lineno, P->line);
      }
    }
  }
  valrepl = potion_source(P, AST_VALUE, repl, PN_NIL, PN_NIL, lineno, P->line);
  args = potion_source(P, AST_LIST,
      PN_PUSH(PN_PUSH(PN_TUP(regex), valrepl),
              potion_source(P, AST_VALUE, global ? PN_TRUE : PN_FALSE,
                            PN_NIL, PN_NIL, lineno, P->line)),
      PN_NIL, PN_NIL, lineno, P->line);
  msg = potion_source(P, AST_MSG, PN_STR("p5subst"), args, PN_NIL, lineno, P->line);
  call = potion_source(P, AST_EXPR, PN_PUSH(PN_TUP(source), msg), PN_NIL, PN_NIL, lineno, P->line);
  assign = potion_source(P, AST_ASSIGN, target, call, PN_NIL, lineno, P->line);
  return potion_source(P, AST_EXPR,
           PN_PUSH(PN_TUP(potion_source(P, AST_EXPR, PN_TUP(assign), PN_NIL, PN_NIL, lineno, P->line)),
                   potion_source(P, AST_MSG, PN_STR("p5subcount"), PN_NIL, PN_NIL, lineno, P->line)),
           PN_NIL, PN_NIL, lineno, P->line);
}
/* X = (pattern, replacement, flags) of the rxs rule */
static PN p5_subst2(Potion *P, long lineno, PN subject, PN x) {
  PN flags = PN_TUPLE_AT(x, 2);
  return p5_subst(P, lineno, subject, PN_TUPLE_AT(x, 0), PN_TUPLE_AT(x, 1),
                  memchr(PN_STR_PTR(flags), 'g', PN_STR_LEN(flags)) != NULL, flags);
}
/* `$s =~ tr/FROM/TO/flags`  =>  `($s = $s->p5tr(FROM, TO, flags))->p5trcount`; with the r flag
 * the new string is the value and $s is not changed */
static PN p5_tr(Potion *P, long lineno, PN subject, PN x) {
  PN target, source = p5_mod_subject(P, subject, &target);
  PN flags = PN_TUPLE_AT(x, 2), args, call, assign;
  args = potion_source(P, AST_LIST,
           PN_PUSH(PN_PUSH(PN_TUP(potion_source(P, AST_VALUE, PN_TUPLE_AT(x, 0), PN_NIL, PN_NIL, lineno, P->line)),
                           potion_source(P, AST_VALUE, PN_TUPLE_AT(x, 1), PN_NIL, PN_NIL, lineno, P->line)),
                   potion_source(P, AST_VALUE, flags, PN_NIL, PN_NIL, lineno, P->line)),
           PN_NIL, PN_NIL, lineno, P->line);
  call = potion_source(P, AST_EXPR,
           PN_PUSH(PN_TUP(source), potion_source(P, AST_MSG, PN_STR("p5tr"), args, PN_NIL, lineno, P->line)),
           PN_NIL, PN_NIL, lineno, P->line);
  if (memchr(PN_STR_PTR(flags), 'r', PN_STR_LEN(flags))) return call;
  assign = potion_source(P, AST_ASSIGN, target, call, PN_NIL, lineno, P->line);
  return potion_source(P, AST_EXPR,
           PN_PUSH(PN_TUP(potion_source(P, AST_EXPR, PN_TUP(assign), PN_NIL, PN_NIL, lineno, P->line)),
                   potion_source(P, AST_MSG, PN_STR("p5trcount"), PN_NIL, PN_NIL, lineno, P->line)),
           PN_NIL, PN_NIL, lineno, P->line);
}

/* chomp $x  =>  $x = $x->p5chomp */
static PN p5_chompx(Potion *P, long lineno, PN line, PN var, const char *meth) {
  PN call = potion_source(P, AST_EXPR,
              PN_PUSH(PN_TUP(var), potion_source(P, AST_MSG, PN_STR(meth), PN_NIL, PN_NIL, lineno, line)),
              PN_NIL, PN_NIL, lineno, line);
  return potion_source(P, AST_EXPR, PN_TUP(potion_source(P, AST_ASSIGN, var, call, PN_NIL, lineno, line)),
                       PN_NIL, PN_NIL, lineno, line);
}

/* chomp's value is the number of characters removed (p5chomp leaves it in $^CHOMPED) */
static PN p5_chomp(Potion *P, long lineno, PN line, PN var) {
  PN asg = p5_chompx(P, lineno, line, var, "p5chomp");
  PN g1 = potion_source(P, AST_MSG, PN_STR("$^CHOMPED"), PN_NIL, PN_NIL, lineno, line);
  PN g2 = potion_source(P, AST_MSG, PN_STR("$^CHOMPED"), PN_NIL, PN_NIL, lineno, line);
  return potion_source(P, AST_OR, potion_source(P, AST_AND, asg, g1, PN_NIL, lineno, line), g2, PN_NIL, lineno, line);
}
/* chop's value is the removed character (p5chop leaves it in $^CHOPPED): (var = var->p5chop) && G || G */
static PN p5_chop(Potion *P, long lineno, PN line, PN var) {
  PN asg = p5_chompx(P, lineno, line, var, "p5chop");
  PN g1 = potion_source(P, AST_MSG, PN_STR("$^CHOPPED"), PN_NIL, PN_NIL, lineno, line);
  PN g2 = potion_source(P, AST_MSG, PN_STR("$^CHOPPED"), PN_NIL, PN_NIL, lineno, line);
  return potion_source(P, AST_OR, potion_source(P, AST_AND, asg, g1, PN_NIL, lineno, line), g2, PN_NIL, lineno, line);
}

/* chomp(my $x = EXPR)  =>  $x = ($x = EXPR)->p5chomp */
static PN p5_chomp_assign(Potion *P, long lineno, PN line, PN var, PN e) {
  PN inner = potion_source(P, AST_ASSIGN, var, e, PN_NIL, lineno, line);
  PN call = potion_source(P, AST_EXPR,
              PN_PUSH(PN_TUP(potion_source(P, AST_EXPR, PN_TUP(inner), PN_NIL, PN_NIL, lineno, line)),
                      potion_source(P, AST_MSG, PN_STR("p5chomp"), PN_NIL, PN_NIL, lineno, line)),
              PN_NIL, PN_NIL, lineno, line);
  return potion_source(P, AST_EXPR, PN_TUP(potion_source(P, AST_ASSIGN, var, call, PN_NIL, lineno, line)),
                       PN_NIL, PN_NIL, lineno, line);
}

/* pos($s): the position after the last m//g match on $s */
/* pos($s) = N */
static PN p5_pos_assign(Potion *P, long lineno, PN line, PN var, PN val) {
  PN args = potion_source(P, AST_LIST, PN_PUSH(PN_PUSH(PN_TUP(potion_source(P, AST_VALUE, PN_S(var, 0), PN_NIL, PN_NIL, lineno, line)), var), val),
                          PN_NIL, PN_NIL, lineno, line);
  return potion_source(P, AST_EXPR, PN_TUP(potion_source(P, AST_MSG, PN_STR("p5posset"), args, PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line);
}
static PN p5_pos(Potion *P, long lineno, PN line, PN var) {
  PN args = potion_source(P, AST_LIST, PN_PUSH(PN_TUP(potion_source(P, AST_VALUE, PN_S(var, 0), PN_NIL, PN_NIL, lineno, line)), var),
                          PN_NIL, PN_NIL, lineno, line);
  return potion_source(P, AST_EXPR, PN_TUP(potion_source(P, AST_MSG, PN_STR("p5pos"), args, PN_NIL, lineno, line)),
                       PN_NIL, PN_NIL, lineno, line);
}
/* the name of the variable a match is made on ("$s", "$_"), the key of its pos(): "" for others */
static PN p5_pos_key(Potion *P, PN subject) {
  PN k = subject;
  while (PN_IS_PTR(k) && PN_VTYPE(k) == PN_TSOURCE && PN_PART(k) == AST_EXPR && PN_IS_TUPLE(PN_S(k, 0)) && PN_TUPLE_LEN(PN_S(k, 0)) == 1)
    k = PN_TUPLE_AT(PN_S(k, 0), 0);
  if (PN_IS_PTR(k) && PN_VTYPE(k) == PN_TSOURCE && PN_PART(k) == AST_MSG && PN_IS_STR(PN_S(k, 0)) && PN_S(k, 1) == PN_NIL)
    return PN_S(k, 0);
  return PN_STR("");
}
/* a /g match is the walking p5matchg(pattern, pos key) in scalar context; a list context
 * (p5_listctx) turns it into p5matchall(pattern) */
static PN p5_matchval(Potion *P, long lineno, PN line, PN subject,
                      PN pattern, int negate) {
  int g = p5_re_g, c = p5_re_c;
  PN args, msg, call;
  p5_re_g = p5_re_c = 0;
  args = potion_source(P, AST_LIST, g ? PN_PUSH(PN_PUSH(PN_TUP(pattern), potion_source(P, AST_VALUE, p5_pos_key(P, subject), PN_NIL, PN_NIL, lineno, line)),
                                                 potion_source(P, AST_VALUE, c ? PN_TRUE : PN_FALSE, PN_NIL, PN_NIL, lineno, line)) : PN_TUP(pattern),
                       PN_NIL, PN_NIL, lineno, line);
  msg = potion_source(P, AST_MSG, PN_STR(g ? "p5matchg" : "p5match"), args, PN_NIL, lineno, line);
  call = potion_source(P, AST_EXPR, PN_PUSH(PN_TUP(subject), msg), PN_NIL, PN_NIL, lineno, line);
  return negate
    ? potion_source(P, AST_NOT, call, PN_NIL, PN_NIL, lineno, line)
    : call;
}
/* die / warn say " at FILE line N.": a statement that mentions them starts with p5setloc(FILE, LINE), which
 * records the position of the statement (perl reports the line of the statement too) */
static PN p5_pat_die, p5_pat_warn;
static PN p5_with_loc(Potion *P, const char *file, long line, PN stmt) {
  PN caps[PN_AST_CAPS], set;
  if (!p5_pat_die) {
    p5_pat_die = potion_source(P, AST_MSG, PN_STR("die"), potion_ast_wild(P, -1), PN_NIL, 0, PN_NIL); potion_gc_root(P, &p5_pat_die);
    p5_pat_warn = potion_source(P, AST_MSG, PN_STR("warn"), potion_ast_wild(P, -1), PN_NIL, 0, PN_NIL); potion_gc_root(P, &p5_pat_warn);
  }
  if (!potion_ast_find(P, p5_pat_die, stmt, caps, 0) && !potion_ast_find(P, p5_pat_warn, stmt, caps, 0)) return PN_TUP(stmt);
  set = potion_source(P, AST_EXPR, PN_TUP(potion_source(P, AST_MSG, PN_STR("p5setloc"),
          potion_source(P, AST_LIST, PN_PUSH(PN_TUP(potion_source(P, AST_VALUE, potion_str(P, file ? file : "-"), PN_NIL, PN_NIL, line, PN_NIL)),
                                             potion_source(P, AST_VALUE, PN_NUM(line), PN_NIL, PN_NIL, line, PN_NIL)), PN_NIL, PN_NIL, line, PN_NIL),
          PN_NIL, line, PN_NIL)), PN_NIL, PN_NIL, line, PN_NIL);
  return PN_PUSH(PN_TUP(set), stmt);
}
/* a statement that is just `$x =~ m//g;` has no context to walk the matches: a plain match, unless the
 * program reads pos() */
static PN p5_voidctx(Potion *P, PN stmt) {
  PN k = stmt, st, m;
  if (p5_pos_used) return stmt;
  while (PN_IS_PTR(k) && PN_VTYPE(k) == PN_TSOURCE && PN_PART(k) == AST_EXPR && PN_IS_TUPLE(PN_S(k, 0))) {
    st = PN_S(k, 0);
    if (PN_TUPLE_LEN(st) == 1 && PN_IS_PTR(PN_TUPLE_AT(st, 0)) && PN_VTYPE(PN_TUPLE_AT(st, 0)) == PN_TSOURCE && PN_PART(PN_TUPLE_AT(st, 0)) == AST_EXPR) { k = PN_TUPLE_AT(st, 0); continue; }
    m = PN_TUPLE_AT(st, PN_TUPLE_LEN(st) - 1);
    if (PN_TUPLE_LEN(st) > 1 && PN_IS_PTR(m) && PN_VTYPE(m) == PN_TSOURCE && PN_PART(m) == AST_MSG && PN_IS_STR(PN_S(m, 0)) && !strcmp(PN_STR_PTR(PN_S(m, 0)), "p5matchg")) {
      PN args = PN_S(m, 1), items = PN_S(args, 0);
      { PN nm = PN_STR("p5match"); PN_SRC(m)->a[0] = PN_SRC(nm); PN_TOUCH(m); }
      { PN a0 = PN_TUP(PN_TUPLE_AT(items, 0)); PN_SRC(args)->a[0] = PN_SRC(a0); PN_TOUCH(args); }
      PN_TOUCH(args); PN_TOUCH(m);
    }
    break;
  }
  return stmt;
}
/* a list context for the item: `m//g` there returns all the matches */
static PN p5_listctx(Potion *P, PN item) {
  PN k = item, st, m;
  while (PN_IS_PTR(k) && PN_VTYPE(k) == PN_TSOURCE && PN_PART(k) == AST_EXPR && PN_IS_TUPLE(PN_S(k, 0))) {
    st = PN_S(k, 0);
    if (PN_TUPLE_LEN(st) == 1 && PN_IS_PTR(PN_TUPLE_AT(st, 0)) && PN_VTYPE(PN_TUPLE_AT(st, 0)) == PN_TSOURCE && PN_PART(PN_TUPLE_AT(st, 0)) == AST_EXPR) { k = PN_TUPLE_AT(st, 0); continue; }
    m = PN_TUPLE_AT(st, PN_TUPLE_LEN(st) - 1);
    if (PN_IS_PTR(m) && PN_VTYPE(m) == PN_TSOURCE && PN_PART(m) == AST_MSG && PN_IS_STR(PN_S(m, 0)) && !strcmp(PN_STR_PTR(PN_S(m, 0)), "p5readdir")) {
      PN_SRC(m)->a[0] = PN_SRC(PN_STR("p5readdirall")); PN_TOUCH(m); /* readdir in list context: all the remaining names */
    } else
    if (PN_IS_PTR(m) && PN_VTYPE(m) == PN_TSOURCE && PN_PART(m) == AST_MSG && PN_IS_STR(PN_S(m, 0)) && !strcmp(PN_STR_PTR(PN_S(m, 0)), "p5match")) {
      PN_SRC(m)->a[0] = PN_SRC(PN_STR("p5matchl")); PN_TOUCH(m); /* the groups, or (1) */
    } else
    if (PN_IS_PTR(m) && PN_VTYPE(m) == PN_TSOURCE && PN_PART(m) == AST_MSG && PN_IS_STR(PN_S(m, 0)) && !strcmp(PN_STR_PTR(PN_S(m, 0)), "p5matchg")) {
      PN args = PN_S(m, 1), items = PN_S(args, 0);
      { PN nm = PN_STR("p5matchall"); PN_SRC(m)->a[0] = PN_SRC(nm); PN_TOUCH(m); }
      { PN a0 = PN_TUP(PN_TUPLE_AT(items, 0)); PN_SRC(args)->a[0] = PN_SRC(a0); PN_TOUCH(args); }
      PN_TOUCH(args); PN_TOUCH(m);
    }
    break;
  }
  return item;
}


/* a . b: string concatenation is (a, b, ...).p5join, one flat node per chain
 * (nested calls would exhaust the registers); '+' is always numeric in p5 */
static PN p5_pat_cat;
static int p5_is_cat(Potion *P, PN n) {
  PN caps[PN_AST_CAPS];
  if (!p5_pat_cat) { /* EXPR[ ?_, MSG p5join ] */
    p5_pat_cat = potion_source(P, AST_EXPR, PN_PUSH(PN_PUSH(PN_TUP0(), potion_ast_wild(P, -1)),
      potion_source(P, AST_MSG, PN_STR("p5join"), potion_ast_wild(P, -1), potion_ast_wild(P, -1), 0, PN_NIL)),
      PN_NIL, PN_NIL, 0, PN_NIL);
    potion_gc_root(P, &p5_pat_cat);
  }
  return potion_ast_match(P, p5_pat_cat, n, caps, 0);
}
static PN p5_cat_item(Potion *P, long lineno, PN line, PN a) {
  a = p5_unparen(P, a); /* "f" . ($x || ""): a parenthesized single item is not a list */
  /* a bare MSG (a variable or an element read like $a[1]) must be an expression */
  if (PN_IS_PTR(a) && PN_VTYPE(a) == PN_TSOURCE && (PN_PART(a) == AST_MSG || PN_PART(a) == AST_ASSIGN)) /* ($x = 1) . "b": the assigned value */
    return potion_source(P, AST_EXPR, PN_TUP(a), PN_NIL, PN_NIL, lineno, line);
  return a;
}
/* join(SEP, LIST): the flattened LIST is the receiver of p5joins(SEP) */
static PN p5_join_node(Potion *P, long lineno, PN line, PN sep, PN l) {
  PN lst;
  { PN_SIZE ji; for (ji = 0; ji < PN_TUPLE_LEN(l); ji++) p5_listctx(P, PN_TUPLE_AT(l, ji)); } /* join's list is in list context */
  if (PN_TUPLE_LEN(l) == 1 && p5_arrayish(P, PN_TUPLE_AT(l, 0))) lst = PN_TUPLE_AT(l, 0);
  else lst = p5_flatten_multi(P, lineno, line, potion_source(P, AST_LIST, l, PN_NIL, PN_NIL, lineno, line));
  return potion_source(P, AST_EXPR,
           PN_PUSH(PN_TUP(lst), potion_source(P, AST_MSG, PN_STR("p5joins"),
             potion_source(P, AST_LIST, PN_TUP(sep), PN_NIL, PN_NIL, lineno, line), PN_NIL, lineno, line)),
           PN_NIL, PN_NIL, lineno, line);
}
/* a '.' chain used as an arithmetic operand ('1 . 1 + 1'): evaluated as a whole */
static PN p5_cat_operand(Potion *P, PN a) {
  return p5_is_cat(P, a) ? potion_source(P, AST_EXPR, PN_TUP(a), PN_NIL, PN_NIL, 0, PN_NIL) : a;
}
static PN p5_cat2(Potion *P, long lineno, PN line, PN a, PN b) {
  b = p5_cat_item(P, lineno, line, b);
  if (p5_is_cat(P, a)) { /* extend the chain */
    PN lst = PN_TUPLE_AT(PN_S(a, 0), 0);
    PN_SRC(lst)->a[0] = PN_SRC(PN_PUSH(PN_S(lst, 0), b)); PN_TOUCH(lst);
    return a;
  }
  a = p5_cat_item(P, lineno, line, a);
  return potion_source(P, AST_EXPR,
           PN_PUSH(PN_TUP(potion_source(P, AST_LIST, PN_PUSH(PN_TUP(a), b), PN_NIL, PN_NIL, lineno, line)),
                   potion_source(P, AST_MSG, PN_STR("p5join"), PN_NIL, PN_NIL, lineno, line)),
           PN_NIL, PN_NIL, lineno, line);
}

/* a | b, a & b, a ^ b: the p5bor/p5band/p5bxor natives (integer operands) */
static PN p5_bitop(Potion *P, long lineno, PN line, const char *name, PN a, PN b) {
  PN args = potion_source(P, AST_LIST,
              PN_PUSH(PN_TUP(p5_cat_item(P, lineno, line, a)), p5_cat_item(P, lineno, line, b)),
              PN_NIL, PN_NIL, lineno, line);
  return potion_source(P, AST_EXPR,
           PN_TUP(potion_source(P, AST_MSG, PN_STR(name), args, PN_NIL, lineno, line)),
           PN_NIL, PN_NIL, lineno, line);
}

/* an integer literal: a tagged integer, a bigint up to 2**64-1 (perl's UV; negative: down to -2**63), else a double */
static PN p5_int_lit(Potion *P, const char *c, long n, int base) {
  char buf[96], *end;
  unsigned long long v;
  int neg;
  if (n <= 0 || n >= (long)sizeof buf) return potion_strtod(P, (char *)c, n);
  memcpy(buf, c, n); buf[n] = '\0';
  neg = buf[0] == '-';
  errno = 0;
  v = strtoull(buf + neg, &end, base);
  if (errno == ERANGE || (neg && v > 9223372036854775808ULL)) return potion_strtod(P, (char *)c, n);
  if (neg) return potion_big_from_i64(P, v == 9223372036854775808ULL ? INT64_MIN : -(int64_t)v);
  return potion_big_from_u64(P, (uint64_t)v);
}
/* numeric literal text without the '_' digit separators (10_000) */
static char p5_numbuf[128];
static const char *p5_num_clean(const char *s, long n, long *outn) {
  long i, k = 0;
  for (i = 0; i < n && k < (long)sizeof(p5_numbuf) - 1; i++)
    if (s[i] != '_') p5_numbuf[k++] = s[i];
  p5_numbuf[k] = 0;
  *outn = k;
  return p5_numbuf;
}

static PN p5_prepend(Potion *P, PN first, PN items) {
  PN t = PN_TUP(first);
  PN_SIZE i;
  for (i = 0; i < PN_TUPLE_LEN(items); i++) t = PN_PUSH(t, PN_TUPLE_AT(items, i));
  return t;
}
/* split PATTERN, STRING [, LIMIT] and printf FORMAT, LIST: lobby calls with padded arguments */
static PN p5_call_padded(Potion *P, long lineno, PN line, const char *name, PN items) {
  PN m = potion_source(P, AST_MSG, PN_STR(name), PN_NIL, PN_NIL, lineno, line);
  PN_SRC(m)->a[1] = PN_SRC(potion_source(P, AST_LIST, p5_pad_args(P, m, items), PN_NIL, PN_NIL, lineno, line)); PN_TOUCH(m);
  return potion_source(P, AST_EXPR, PN_TUP(m), PN_NIL, PN_NIL, lineno, line);
}
/* split ' ', ...: the awk mode pattern is the marker "\001" (a regex /\s+/ would not skip
 * leading white space) */
static PN p5_split_awk(Potion *P, long lineno, PN line, PN e) {
  PN n = e;
  while (PN_IS_PTR(n) && PN_VTYPE(n) == PN_TSOURCE && PN_PART(n) == AST_EXPR &&
         PN_IS_TUPLE(PN_S(n, 0)) && PN_TUPLE_LEN(PN_S(n, 0)) == 1)
    n = PN_TUPLE_AT(PN_S(n, 0), 0);
  if (PN_IS_PTR(n) && PN_VTYPE(n) == PN_TSOURCE && PN_PART(n) == AST_VALUE &&
      PN_IS_STR(PN_S(n, 0)) && PN_STR_LEN(PN_S(n, 0)) == 1 && PN_STR_PTR(PN_S(n, 0))[0] == ' ')
    return potion_source(P, AST_VALUE, PN_STRN("\001", 1), PN_NIL, PN_NIL, lineno, line);
  return e;
}
/* printf FORMAT, LIST: print(sprintf(...)) */
/* the sprintf call of printf LIST: `printf FH ("%s", 1)` hands over the one list literal */
static PN p5_sprintf_call(Potion *P, long lineno, PN line, PN items) {
  if (PN_IS_TUPLE(items) && PN_TUPLE_LEN(items) == 1 && p5_is_listlit(P, PN_TUPLE_AT(items, 0))) {
    PN l = p5_unparen(P, PN_TUPLE_AT(items, 0));
    if (PN_IS_PTR(l) && PN_VTYPE(l) == PN_TSOURCE && PN_PART(l) == AST_EXPR && PN_IS_TUPLE(PN_S(l, 0)) && PN_TUPLE_LEN(PN_S(l, 0)) == 1) l = PN_TUPLE_AT(PN_S(l, 0), 0);
    if (PN_IS_PTR(l) && PN_VTYPE(l) == PN_TSOURCE && PN_PART(l) == AST_LIST && PN_IS_TUPLE(PN_S(l, 0))) items = PN_S(l, 0);
  }
  return p5_call_padded(P, lineno, line, "sprintf", items);
}
/* printf HANDLE LIST: the formatted text printed with `method` (p5print / eprint / p5lprint) */
static PN p5_printf_fh(Potion *P, long lineno, PN line, PN h, const char *method, PN items) {
  PN fmt = p5_sprintf_call(P, lineno, line, items);
  if (h == PN_NIL)
    return potion_source(P, AST_EXPR, PN_PUSH(PN_TUP(fmt), potion_source(P, AST_MSG, PN_STR(method), PN_NIL, PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line);
  return potion_source(P, AST_EXPR, PN_PUSH(PN_TUP(h), potion_source(P, AST_MSG, PN_STR(method),
           potion_source(P, AST_LIST, PN_TUP(potion_source(P, AST_EXPR, PN_TUPIF(fmt), PN_NIL, PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line),
           PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line);
}
static PN p5_printf(Potion *P, long lineno, PN line, PN items) {
  return potion_source(P, AST_EXPR,
           PN_PUSH(PN_TUP(p5_sprintf_call(P, lineno, line, items)),
                   potion_source(P, AST_MSG, PN_STR("p5lprint"), PN_NIL, PN_NIL, lineno, line)),
           PN_NIL, PN_NIL, lineno, line);
}

static PN p5_scalar_ref(Potion *P, long lineno, PN line, PN var) {
  PN body = potion_source(P, AST_BLOCK,
              PN_TUP(potion_source(P, AST_EXPR, PN_TUP(var), PN_NIL, PN_NIL, lineno, line)),
              PN_NIL, PN_NIL, lineno, line);
  PN proto = p5_sub_proto(P, lineno, line, body);
  PN args = potion_source(P, AST_LIST,
              PN_TUP(potion_source(P, AST_EXPR, PN_TUP(proto), PN_NIL, PN_NIL, lineno, line)),
              PN_NIL, PN_NIL, lineno, line);
  return potion_source(P, AST_EXPR,
           PN_TUP(potion_source(P, AST_MSG, PN_STR("p5mkref"), args, PN_NIL, lineno, line)),
           PN_NIL, PN_NIL, lineno, line);
}

/* @a[1,2] / @h{'a','b'}: the index list is the receiver, the container the argument.
 * VALUES (not nil) makes it the assignment @a[1,2] = LIST: INDICES.p5sliceset(container, values) */
static PN p5_slice_op(Potion *P, long lineno, PN line, PN container, PN items, PN values) {
  PN lst = potion_source(P, AST_LIST, items, PN_NIL, PN_NIL, lineno, line);
  PN cont = potion_source(P, AST_EXPR,
              PN_TUP(potion_source(P, AST_MSG, container, PN_NIL, PN_NIL, lineno, line)),
              PN_NIL, PN_NIL, lineno, line);
  PN args = potion_source(P, AST_LIST, PN_TUP(cont), PN_NIL, PN_NIL, lineno, line);
  if (values != PN_NIL) args = potion_source(P, AST_LIST, PN_PUSH(PN_TUP(cont), values), PN_NIL, PN_NIL, lineno, line);
  return potion_source(P, AST_EXPR,
           PN_PUSH(PN_TUP(lst), potion_source(P, AST_MSG, PN_STR(values != PN_NIL ? "p5sliceset" : "p5slice"), args, PN_NIL, lineno, line)),
           PN_NIL, PN_NIL, lineno, line);
}
/* CONTAINER->@[LIST]: the message p5sliceof(indices) */
static PN p5_sliceof(Potion *P, long lineno, PN line, PN items) {
  PN lst = potion_source(P, AST_EXPR, PN_TUP(potion_source(P, AST_LIST, items, PN_NIL, PN_NIL, lineno, line)),
             PN_NIL, PN_NIL, lineno, line);
  return potion_source(P, AST_MSG, PN_STR("p5sliceof"),
           potion_source(P, AST_LIST, PN_TUP(lst), PN_NIL, PN_NIL, lineno, line), PN_NIL, lineno, line);
}
static PN p5_slice(Potion *P, long lineno, PN line, PN container, PN items) {
  return p5_slice_op(P, lineno, line, container, items, PN_NIL);
}

static int p5_ofs_used; /* the source mentions `$,` */
static int p5_lsep_used; /* ... or `$\"` */
/* the separator of an interpolated list: " ", or the global $\" when the program mentions it */
static PN p5_listsep(Potion *P) {
  if (p5_lsep_used)
    return potion_source(P, AST_EXPR, PN_TUP(potion_source(P, AST_MSG, PN_STR("$\""), PN_NIL, PN_NIL, 0, PN_NIL)), PN_NIL, PN_NIL, 0, PN_NIL);
  return potion_source(P, AST_VALUE, PN_STR(" "), PN_NIL, PN_NIL, 0, PN_NIL);
}
/* print/say LIST: the items concatenated (join ""); a lone item is itself */
/* print <>; print <FH>;: the whole input in list context. The readline node is EXPR[(.., MSG p5argvread|p5readline)];
 * returns the same node with the "all lines" message, or nil */
static PN p5_readline_all(Potion *P, PN item) {
  PN t, last;
  if (!(PN_IS_PTR(item) && PN_VTYPE(item) == PN_TSOURCE && PN_PART(item) == AST_EXPR && PN_IS_TUPLE(PN_S(item, 0)))) return PN_NIL;
  t = PN_S(item, 0);
  while (PN_TUPLE_LEN(t) == 1 && PN_IS_PTR(PN_TUPLE_AT(t, 0)) && PN_VTYPE(PN_TUPLE_AT(t, 0)) == PN_TSOURCE &&
         PN_PART(PN_TUPLE_AT(t, 0)) == AST_EXPR && PN_IS_TUPLE(PN_S(PN_TUPLE_AT(t, 0), 0))) /* EXPR[EXPR[..]] */
    t = PN_S(PN_TUPLE_AT(t, 0), 0);
  if (!PN_TUPLE_LEN(t)) return PN_NIL;
  last = PN_TUPLE_AT(t, PN_TUPLE_LEN(t) - 1);
  if (!(PN_IS_PTR(last) && PN_VTYPE(last) == PN_TSOURCE && PN_PART(last) == AST_MSG && PN_IS_STR(PN_S(last, 0)))) return PN_NIL;
  if (!strcmp(PN_STR_PTR(PN_S(last, 0)), "p5argvread") || !strcmp(PN_STR_PTR(PN_S(last, 0)), "p5readline") ||
      !strcmp(PN_STR_PTR(PN_S(last, 0)), "readpipe") || !strcmp(PN_STR_PTR(PN_S(last, 0)), "p5readdir")) {
    PN nt = PN_TUP0(); PN_SIZE k;
    const char *ln = PN_STR_PTR(PN_S(last, 0));
    for (k = 0; k + 1 < PN_TUPLE_LEN(t); k++) nt = PN_PUSH(nt, PN_TUPLE_AT(t, k));
    nt = PN_PUSH(nt, potion_source(P, AST_MSG, PN_STR(!strcmp(ln, "p5argvread") ? "p5argvall" : !strcmp(ln, "readpipe") ? "readpipelist" : !strcmp(ln, "p5readdir") ? "p5readdirall" : "p5readall"),
                                   PN_S(last, 1), PN_NIL, 0, PN_NIL));
    return potion_source(P, AST_EXPR, nt, PN_NIL, PN_NIL, 0, PN_NIL);
  }
  return PN_NIL;
}
static PN p5_cat(Potion *P, long lineno, PN line, PN l) {
  PN lst;
  if (PN_TUPLE_LEN(l) == 1 && p5_readline_all(P, PN_TUPLE_AT(l, 0)) != PN_NIL) {
    return potion_source(P, AST_EXPR, PN_PUSH(PN_TUP(p5_readline_all(P, PN_TUPLE_AT(l, 0))),
             potion_source(P, AST_MSG, PN_STR("p5join"), PN_NIL, PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line);
  }
  if (PN_TUPLE_LEN(l) == 1) {
    PN only = PN_TUPLE_AT(l, 0);
    if (!p5_arrayish(P, only)) return p5_unparen(P, only);
    lst = only; /* print @a: the elements, joined */
  } else { /* print "x", @a: array items are flattened, (EXPR) items are plain values */
    PN un = PN_TUP0(); PN_SIZE k;
    for (k = 0; k < PN_TUPLE_LEN(l); k++) un = PN_PUSH(un, p5_unparen(P, PN_TUPLE_AT(l, k)));
    lst = p5_flatten_multi(P, lineno, line, potion_source(P, AST_LIST, un, PN_NIL, PN_NIL, lineno, line));
  }
  if (p5_ofs_used) /* $, : the output field separator (only when the program mentions it) */
    return potion_source(P, AST_EXPR, PN_PUSH(PN_TUP(lst), potion_source(P, AST_MSG, PN_STR("p5joins"),
             potion_source(P, AST_LIST, PN_TUP(potion_source(P, AST_EXPR, PN_TUP(potion_source(P, AST_MSG, PN_STR("$,"), PN_NIL, PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line),
             PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line);
  return potion_source(P, AST_EXPR, PN_PUSH(PN_TUP(lst), potion_source(P, AST_MSG, PN_STR("p5join"), PN_NIL, PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line);
}

/* 'use integer' / 'no integer': $^H |= 1 / $^H &= ~1 (HINT_INTEGER), restored at
 * the end of the enclosing block (block scoped) */
static PN p5_integer_pragma(Potion *P, long lineno, PN line, int on) {
  PN v = potion_source(P, AST_VALUE, on ? PN_NUM(1) : PN_NUM(-2), PN_NIL, PN_NIL, lineno, line);
  PN h = p5_hints_read(P, lineno, line);
  PN val = p5_bitop(P, lineno, line, on ? "p5bor" : "p5band", h, v);
  p5_local_note(P, PN_STR("$^H"));
  return p5_hints_call(P, lineno, line, val);
}

/* (yytext is NUL terminated; yyleng is not valid inside a predicate)
 * lowercase bareword file handles ('open(tmp, ...)' then 'print tmp "x"') are
 * only recognised after their open(), else 'print lc "X"' would be a handle */
static char p5_fh_names[64][32];
static int p5_fh_n;
static int p5_fh_known(const char *s) {
  int i, n = strlen(s);
  for (i = 0; i < p5_fh_n; i++)
    if ((int)strlen(p5_fh_names[i]) == n && !memcmp(p5_fh_names[i], s, n)) return 1;
  return 0;
}
static int p5_fh_note(const char *s) {
  int n = strlen(s);
  if (n < 32 && p5_fh_n < 64 && !p5_fh_known(s)) {
    memcpy(p5_fh_names[p5_fh_n], s, n);
    p5_fh_names[p5_fh_n++][n] = 0;
  }
  return 1;
}

/* open(FH, MODE, PATH) / open(FH, "<PATH"): FH = p5open3(MODE, PATH) / p5open2(SPEC) */
static PN p5_open(Potion *P, long lineno, PN line, PN target, PN args) {
  long n = PN_TUPLE_LEN(args);
  PN lst, call;
  if (n > 2) n = 2;
  if (n < 1) n = 1;
  if (n == 1)
    lst = potion_source(P, AST_LIST, PN_TUP(PN_TUPLE_AT(args, 0)), PN_NIL, PN_NIL, lineno, line);
  else
    lst = potion_source(P, AST_LIST, PN_PUSH(PN_TUP(PN_TUPLE_AT(args, 0)), PN_TUPLE_AT(args, 1)),
                        PN_NIL, PN_NIL, lineno, line);
  call = potion_source(P, AST_EXPR,
           PN_TUP(potion_source(P, AST_MSG, PN_STR(n == 1 ? "p5open2" : "p5open3"), lst, PN_NIL, lineno, line)),
           PN_NIL, PN_NIL, lineno, line);
  /* an EXPR: a bare assignment as a call argument (ok(open(my $fh, ..))) is lost */
  return potion_source(P, AST_EXPR, PN_TUP(potion_source(P, AST_ASSIGN, target, call, PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line);
}

/* 'while (<FH>) {...}' => while (defined($_ = <FH>)) {...}; same for
 * 'while (my $l = <FH>)' */
/* the condition list of while (VAR = <>): (VAR = <>) != undef */
static PN p5_rl_cond(Potion *P, long lineno, PN line, PN var, PN rd) {
  PN asg = potion_source(P, AST_ASSIGN, var, rd, PN_NIL, lineno, line);
  PN cond = potion_source(P, AST_NEQ, asg,
              potion_source(P, AST_VALUE, PN_NIL, PN_NIL, PN_NIL, lineno, line), PN_NIL, lineno, line);
  return potion_source(P, AST_LIST, PN_TUP(cond), PN_NIL, PN_NIL, lineno, line);
}
/* while (VAR = <>) BODY continue CB: like p5_while_cont, the readline condition is not a truth test */
static PN p5_while_rl_cont(Potion *P, long lineno, PN line, PN var, PN rd, PN body, PN cont) {
  PN l = p5_rl_cond(P, lineno, line, var, rd);
  /* the variable must exist before the test: an assignment inside the condition would make a local of its own */
  PN decl = potion_source(P, AST_EXPR, PN_TUP(potion_source(P, AST_ASSIGN, var, potion_source(P, AST_VALUE, PN_NIL, PN_NIL, PN_NIL, lineno, line), PN_NIL, lineno, line)), PN_NIL, PN_NIL, lineno, line);
  return p5_cfor(P, lineno, line, decl, PN_TUPLE_AT(PN_S(l, 0), 0), cont, body);
}
static PN p5_while_rl(Potion *P, long lineno, PN line, PN var, PN rd, PN block) {
  PN l = p5_rl_cond(P, lineno, line, var, rd);
  PN m = potion_source(P, AST_MSG, PN_STR("while"), PN_NIL, PN_NIL, lineno, line);
  return p5_special_stmt(P, lineno, m, l, block);
}

/* ---- perl 5.38 `use feature 'class'`: class / field / method / ADJUST ----
 *
 * A class block becomes a Potion class: `Name = class (ctor params) { field inits;
 * ADJUST blocks }` plus `Name meth = proto` for every method. A scalar field `$x` is
 * the ivar /x, so a reference to `$x` inside a method, ADJUST block or field
 * initializer is rewritten to a PATH node (p5_subst_fields); `field @a`/`%h` are
 * tuples/tables held in the ivar, a method binds them to local aliases
 * on entry. `$self` is `my $self = self;` on entry. :param fields are optional ctor
 * parameters (PN_P5NOARG = not given) named $__p_NAME, Name->new(k => v, ..) is
 * rewritten at parse time to Name(positional args) when the class is known.
 *
 * A class body item is a tuple: (1 field: varname param dkind dexpr reader)
 * (2 method: name siglist-or-nil body) (3 ADJUST: body) (4 other statement: stmt). */
#define P5_MAXCLS 128
#define P5_MAXPAR 32
typedef struct { char name[64]; int n; char par[P5_MAXPAR][48]; } p5_class_t;
static p5_class_t p5_classes[P5_MAXCLS];
static int p5_nclasses;
static p5_class_t *p5_cls_cur;

static p5_class_t *p5_class_find(const char *name, size_t len) {
  int i;
  for (i = 0; i < p5_nclasses; i++)
    if (strlen(p5_classes[i].name) == len && !memcmp(p5_classes[i].name, name, len))
      return &p5_classes[i];
  return NULL;
}

/* action at the start of `class NAME [:isa(PARENT)]`: (re)start the record of the class */
static void p5_class_begin(PN name, PN parent) {
  p5_class_t *c = p5_class_find(PN_STR_PTR(name), PN_STR_LEN(name)), *par = NULL;
  p5_cls_cur = NULL;
  if (!c) {
    if (p5_nclasses == P5_MAXCLS || PN_STR_LEN(name) >= sizeof(c->name)) return;
    c = &p5_classes[p5_nclasses++];
    memcpy(c->name, PN_STR_PTR(name), PN_STR_LEN(name)); c->name[PN_STR_LEN(name)] = 0;
  }
  c->n = 0;
  if (parent != PN_NIL) par = p5_class_find(PN_STR_PTR(parent), PN_STR_LEN(parent));
  if (par) {
    memcpy(c->par, par->par, sizeof(c->par[0]) * par->n);
    c->n = par->n;
  }
  p5_cls_cur = c;
}

/* a :param field: the next positional constructor parameter */
static void p5_class_param(PN ext) {
  if (p5_cls_cur && p5_cls_cur->n < P5_MAXPAR && PN_STR_LEN(ext) < sizeof(p5_cls_cur->par[0])) {
    memcpy(p5_cls_cur->par[p5_cls_cur->n], PN_STR_PTR(ext), PN_STR_LEN(ext));
    p5_cls_cur->par[p5_cls_cur->n++][PN_STR_LEN(ext)] = 0;
  }
}

#define CS(t, a, b, c)  potion_source(P, AST_##t, (a), (b), (c), lineno, line)
#define CS1(t, a)       CS(t, a, PN_NIL, PN_NIL)
#define CS2(t, a, b)    CS(t, a, b, PN_NIL)
#define CEXPR(x)        CS1(EXPR, PN_TUP(x))
#define CMSG(n)         CS(MSG, PN_STR(n), PN_NIL, PN_NIL)

/* the item kind of a class body item, 0 for anything else (empty statements) */
static int p5_item_kind(PN it) {
  return PN_IS_TUPLE(it) && PN_TUPLE_LEN(it) > 0 && !PN_IS_PTR(PN_TUPLE_AT(it, 0))
         ? (int)PN_INT(PN_TUPLE_AT(it, 0)) : 0;
}

static int p5_in_names(PN names, PN n) {
  long i;
  for (i = 0; i < (long)PN_TUPLE_LEN(names); i++)
    if (!strcmp(PN_STR_PTR(PN_TUPLE_AT(names, i)), PN_STR_PTR(n))) return 1;
  return 0;
}

/* is N a plain read/write of one of the scalar fields: MSG("$x") without arguments? */
static PN p5_pat_fieldref;
static int p5_is_field_ref(Potion *P, PN n, PN fields) {
  PN caps[PN_AST_CAPS];
  if (!p5_pat_fieldref) { /* a plain variable MSG */
    p5_pat_fieldref = potion_source(P, AST_MSG, potion_ast_wild(P, 0), PN_NIL, PN_NIL, 0, PN_NIL);
    potion_gc_root(P, &p5_pat_fieldref);
  }
  return potion_ast_match(P, p5_pat_fieldref, n, caps, 0) && PN_IS_STR(caps[0]) &&
         PN_STR_PTR(caps[0])[0] == '$' && p5_in_names(fields, caps[0]);
}

/* the ivar name of the field VN ("$x"): x, or Class::x in a class with a parent
 * (PFX = "Class::"), so that a field does not share its slot with a parent class field */
static PN p5_ivname(Potion *P, PN pfx, PN vn) {
  char buf[256];  /* an interned string: potion_str_format makes a bytes object, which has no uniq */
  int l;
  if (pfx == PN_NIL) return PN_STRN(PN_STR_PTR(vn) + 1, PN_STR_LEN(vn) - 1);
  l = snprintf(buf, sizeof(buf), "%s%s", PN_STR_PTR(pfx), PN_STR_PTR(vn) + 1);
  return PN_STRN(buf, l < (int)sizeof(buf) ? l : (int)sizeof(buf) - 1);
}

static PN p5_field_path(Potion *P, PN ref, PN pfx) {
  return potion_source(P, AST_PATH, p5_ivname(P, pfx, PN_S(ref, 0)), PN_NIL, PN_NIL,
                       PN_SRC(ref)->loc.lineno, PN_SRC(ref)->line);
}

/* rewrite the references to the scalar FIELDS in the tree NODE to ivar paths. In an
 * EXPR tuple only the first element can be a variable, the later ones are messages. */
static void p5_subst_fields(Potion *P, PN node, PN fields, int in_expr, PN pfx) {
  long i;
  if (!PN_IS_PTR(node)) return;
  if (PN_IS_TUPLE(node)) {
    for (i = 0; i < (long)PN_TUPLE_LEN(node); i++) {
      PN el = PN_TUPLE_AT(node, i);
      if (p5_is_field_ref(P, el, fields) && !(in_expr && i > 0)) {
        PN_TUPLE_AT(node, i) = p5_field_path(P, el, pfx);
        PN_TOUCH(node);
      } else
        p5_subst_fields(P, el, fields, 0, pfx);
    }
  } else if (PN_VTYPE(node) == PN_TSOURCE) {
    if (PN_PART(node) == AST_INC) {
      /* $x++ / $x-- on a field: INC only knows locals, so $x = $x + 1 */
      PN lhs = PN_S(node, 0);
      while (PN_IS_PTR(lhs) && PN_VTYPE(lhs) == PN_TSOURCE && PN_PART(lhs) == AST_EXPR &&
             PN_IS_TUPLE(PN_S(lhs, 0)) && PN_TUPLE_LEN(PN_S(lhs, 0)) == 1)
        lhs = PN_TUPLE_AT(PN_S(lhs, 0), 0);
      if (p5_is_field_ref(P, lhs, fields)) {
        PN d = (PN)((_PN)PN_S(node, 1) | PN_FINTEGER), path = p5_field_path(P, lhs, pfx);
        long ln = PN_SRC(lhs)->loc.lineno;
        PN ln_line = PN_SRC(lhs)->line;
        PN plus = potion_source(P, AST_PLUS, potion_source(P, AST_EXPR, PN_TUP(p5_field_path(P, lhs, pfx)),
                                  PN_NIL, PN_NIL, ln, ln_line),
                                potion_source(P, AST_VALUE, d, PN_NIL, PN_NIL, ln, ln_line),
                                PN_NIL, ln, ln_line);
        PN_SRC(node)->part = AST_ASSIGN;
        { PN ex = potion_source(P, AST_EXPR, PN_TUP(path), PN_NIL, PN_NIL, ln, ln_line); PN_SRC(node)->a[0] = PN_SRC(ex); PN_TOUCH(node); }
        PN_SRC(node)->a[1] = PN_SRC(plus); PN_TOUCH(node);
        PN_TOUCH(node);
      }
    }
    for (i = 0; i < 3; i++) {
      PN ch = PN_S(node, i);
      if (p5_is_field_ref(P, ch, fields)) {
        PN_SRC(node)->a[i] = PN_SRC(p5_field_path(P, ch, pfx));
        PN_TOUCH(node);
      } else
        p5_subst_fields(P, ch, fields, PN_PART(node) == AST_EXPR && i == 0, pfx);
    }
  }
}

/* the variables named in a signature list (the p5-siglist LIST node) */
static PN p5_sig_names(Potion *P, PN sig) {
  PN names = PN_TUP0();
  long i;
  if (sig == PN_NIL || PN_S(sig, 0) == PN_NIL || !PN_IS_TUPLE(PN_S(sig, 0))) return names;
  for (i = 0; i < (long)PN_TUPLE_LEN(PN_S(sig, 0)); i++) {
    PN e = PN_TUPLE_AT(PN_S(sig, 0), i);
    if (PN_IS_STR(e)) names = PN_PUSH(names, e);
  }
  return names;
}

/* the statements of a method / ADJUST body, with field references rewritten and
 * `my $self = self;` plus aliases for the array and hash fields in front of them.
 * SIGVARS are the parameter names, which shadow fields. */
static PN p5_class_body(Potion *P, long lineno, PN line, PN body, PN fields, PN aliases, PN sigvars, PN pfx) {
  PN use = PN_TUP0(), stmts = PN_TUP0();
  long i;
  for (i = 0; i < (long)PN_TUPLE_LEN(fields); i++)
    if (!p5_in_names(sigvars, PN_TUPLE_AT(fields, i))) use = PN_PUSH(use, PN_TUPLE_AT(fields, i));
  p5_subst_fields(P, body, use, 0, pfx);
  stmts = PN_PUSH(stmts, CS2(ASSIGN, CS1(MSG, PN_STR("$self")), CEXPR(CMSG("self"))));
  for (i = 0; i < (long)PN_TUPLE_LEN(aliases); i++) {
    PN nm = PN_TUPLE_AT(aliases, i);
    if (p5_in_names(sigvars, nm)) continue;
    stmts = PN_PUSH(stmts, CS2(ASSIGN, CS1(MSG, nm),
                   CEXPR(CS1(PATH, p5_ivname(P, pfx, nm)))));
  }
  return stmts;
}

/* BODY (a BLOCK node) prefixed with the statements PRE */
static PN p5_prepend_stmts(Potion *P, PN body, PN pre) {
  PN stmts = pre;
  long i;
  if (PN_S(body, 0) != PN_NIL)
    for (i = 0; i < (long)PN_TUPLE_LEN(PN_S(body, 0)); i++)
      stmts = PN_PUSH(stmts, PN_TUPLE_AT(PN_S(body, 0), i));
  PN_SRC(body)->a[0] = PN_SRC(stmts); PN_TOUCH(body);
  PN_TOUCH(body);
  return body;
}

/* A method is called through a message send, whose call site does not know the callee's
 * defaults ($m = 0 in `method f($n, $m = 0)`): a defaulted parameter is received as
 * PN_P5NOARG, the sentinel the call sites pad with, and the body assigns the default. Returns
 * these `$m = DEFAULT if $m == PN_P5NOARG` statements. */
static PN p5_sig_defaults(Potion *P, long lineno, PN line, PN sig) {
  PN stmts = PN_TUP0(), t;
  long i, n;
  if (sig == PN_NIL || PN_S(sig, 0) == PN_NIL || !PN_IS_TUPLE(PN_S(sig, 0))) return stmts;
  t = PN_S(sig, 0);
  n = PN_TUPLE_LEN(t);
  for (i = 0; i < n; i++) {
    PN nm = PN_TUPLE_AT(t, i);
    if (PN_IS_PTR(nm) && PN_IS_STR(nm) && i + 2 < n && !PN_IS_PTR(PN_TUPLE_AT(t, i + 1)) &&
        PN_INT(PN_TUPLE_AT(t, i + 1)) == ':') {
      PN dv = PN_TUPLE_AT(t, i + 2);
      stmts = PN_PUSH(stmts, CS2(AND,
                CS2(EQ, CEXPR(CS1(MSG, nm)), CS1(VALUE, PN_P5NOARG)),
                CS2(ASSIGN, CEXPR(CS1(MSG, nm)), CS1(VALUE, dv))));
      PN_TUPLE_AT(t, i + 2) = PN_P5NOARG;
      PN_TOUCH(t);
      i += 2;
    }
  }
  return stmts;
}

static PN p5_class_method(Potion *P, long lineno, PN line, PN sig, PN body, PN fields, PN aliases, PN pfx) {
  PN pre = p5_class_body(P, lineno, line, body, fields, aliases, p5_sig_names(P, sig), pfx), proto;
  long i;
  p5_note_args(P);
  if (sig != PN_NIL) {
    PN defs = p5_sig_defaults(P, lineno, line, sig);
    for (i = 0; i < (long)PN_TUPLE_LEN(defs); i++) pre = PN_PUSH(pre, PN_TUPLE_AT(defs, i));
    proto = CS2(PROTO, sig, body);
    p5_prepend_stmts(P, body, pre);
  } else {
    proto = p5_sub_proto(P, lineno, line, body);
    p5_prepend_stmts(P, PN_S(proto, 1), pre);
  }
  return proto;
}

/* a field item (1 varname param dkind dexpr reader) */
static PN p5_class_field_item(Potion *P, PN v, PN attrs, int dkind, PN dexpr) {
  PN item = PN_TUP(PN_NUM(1)), param = PN_NIL, reader = PN_NIL;
  char nmb[96]; /* a copy: PN_PUSH below may move the GC string */
  const char *nm = nmb;
  long i;
  snprintf(nmb, sizeof nmb, "%.*s", (int)PN_STR_LEN(PN_S(v, 0)), PN_STR_PTR(PN_S(v, 0)));
  p5_see(nm, PN_STR_LEN(PN_S(v, 0))); /* a field is declared: no program level autovivification */
  item = PN_PUSH(item, PN_S(v, 0));
  if (attrs != PN_NIL)
    for (i = 0; i < (long)PN_TUPLE_LEN(attrs); i++) {
      PN a = PN_TUPLE_AT(attrs, i), ext = PN_TUPLE_AT(a, 1);
      if (ext == PN_NIL) ext = PN_STRN((char *)nm + 1, strlen(nm + 1));
      if (!strcmp(PN_STR_PTR(PN_TUPLE_AT(a, 0)), "param")) {
        param = ext;
        p5_class_param(ext);
      } else reader = ext;
    }
  item = PN_PUSH(item, param);
  item = PN_PUSH(item, PN_NUM(dkind));
  item = PN_PUSH(item, dexpr);
  return PN_PUSH(item, reader);
}

/* (name sig ctor) of every class assembled so far: a subclass starts with the
 * parameters and the field initializers/ADJUST blocks of its parent. */
static PN p5_cls_store = PN_NIL;

static PN p5_cls_get(PN name) {
  long i;
  if (p5_cls_store == PN_NIL) return PN_NIL;
  for (i = 0; i < (long)PN_TUPLE_LEN(p5_cls_store); i++) {
    PN e = PN_TUPLE_AT(p5_cls_store, i);
    if (!strcmp(PN_STR_PTR(PN_TUPLE_AT(e, 0)), PN_STR_PTR(name))) return e;
  }
  return PN_NIL;
}

static void p5_cls_put(Potion *P, PN name, PN sig, PN ctor) {
  PN e = PN_PUSH(PN_PUSH(PN_TUP(name), sig), ctor);
  if (p5_cls_store == PN_NIL) {
    p5_cls_store = PN_TUP0();
    potion_gc_root(P, &p5_cls_store);
  }
  p5_cls_store = PN_PUSH(p5_cls_store, e);
}

static PN p5_tuple_copy(Potion *P, PN t) {
  PN c = PN_TUP0();
  long i;
  for (i = 0; i < (long)PN_TUPLE_LEN(t); i++) c = PN_PUSH(c, PN_TUPLE_AT(t, i));
  return c;
}

/* the assembled class: head is (name parent), items the tuple of body items (or NIL) */
static PN p5_class_end(Potion *P, long lineno, PN line, PN head, PN items) {
  PN name = PN_TUPLE_AT(head, 0), parent = PN_TUPLE_AT(head, 1), pfx = PN_NIL, pdata = PN_NIL;
  PN fields = PN_TUP0(), aliases = PN_TUP0(), outer = PN_TUP0(), sig = PN_TUP0(), ctor = PN_TUP0(),
     meths = PN_TUP0(), stmts, cls, recv;
  PN noarg = potion_source(P, AST_VALUE, PN_P5NOARG, PN_NIL, PN_NIL, lineno, line);
  long n = items == PN_NIL ? 0 : (long)PN_TUPLE_LEN(items), i;
  if (parent != PN_NIL) {
    pdata = p5_cls_get(parent);
    if (pdata == PN_NIL)
      potion_syntax_error(P, PN_SRC(PN_AST_(MSG, name)), "Class :isa attribute requires a class but \"%s\" is not one",
                          PN_STR_PTR(parent));
    pfx = PN_STRCAT(PN_STR_PTR(name), "::");
    sig = p5_tuple_copy(P, PN_TUPLE_AT(pdata, 1));
    ctor = p5_tuple_copy(P, PN_TUPLE_AT(pdata, 2));
  }
  for (i = 0; i < n; i++) {
    PN it = PN_TUPLE_AT(items, i);
    if (p5_item_kind(it) == 1) {
      PN vn = PN_TUPLE_AT(it, 1);
      if (PN_STR_PTR(vn)[0] == '$') fields = PN_PUSH(fields, vn);
      else aliases = PN_PUSH(aliases, vn);
    }
  }
  for (i = 0; i < n; i++) {
    PN it = PN_TUPLE_AT(items, i);
    switch (p5_item_kind(it)) {
    case 1: {
      PN vn = PN_TUPLE_AT(it, 1), param = PN_TUPLE_AT(it, 2), dexpr = PN_TUPLE_AT(it, 4),
         reader = PN_TUPLE_AT(it, 5);
      int dkind = (int)PN_INT(PN_TUPLE_AT(it, 3)), sc = PN_STR_PTR(vn)[0] == '$';
      PN ivn = p5_ivname(P, pfx, vn), pv;
      PN lhs = CEXPR(CS1(PATH, ivn));
      if (dexpr != PN_NIL) p5_subst_fields(P, dexpr, fields, 0, pfx);
      if (param != PN_NIL) {
        char pn[64];
        snprintf(pn, sizeof(pn), "$__p_%s", PN_STR_PTR(param));
        pv = CS1(MSG, PN_STRN(pn, strlen(pn)));
        sig = PN_PUSH(PN_PUSH(PN_PUSH(sig, PN_STRN(pn, strlen(pn))), PN_NUM(':')), PN_P5NOARG);
        ctor = PN_PUSH(ctor, CS2(ASSIGN, lhs, CEXPR(pv)));
        if (dkind == 0) {
          char msg[160];
          snprintf(msg, sizeof(msg), "Required parameter '%s' is missing for \"%s\" constructor",
                   PN_STR_PTR(param), PN_STR_PTR(name));
          ctor = PN_PUSH(ctor, CS2(AND,
                   CS2(EQ, CEXPR(CS1(MSG, PN_STRN(pn, strlen(pn)))), noarg),
                   CEXPR(CS2(MSG, PN_STR("die"), CS1(LIST,
                     PN_TUP(CEXPR(CS1(VALUE, PN_STRN(msg, strlen(msg))))))))));
        } else {
          PN cond = CS2(EQ, CEXPR(CS1(MSG, PN_STRN(pn, strlen(pn)))), noarg);
          if (dkind == 2)
            cond = CS2(OR, cond, CS2(EQ, CEXPR(CS1(MSG, PN_STRN(pn, strlen(pn)))),
                                     CS1(VALUE, PN_NIL)));
          else if (dkind == 3)
            cond = CS2(OR, cond, CS1(NOT, CEXPR(CS1(MSG, PN_STRN(pn, strlen(pn))))));
          ctor = PN_PUSH(ctor, CS2(AND, cond, CS2(ASSIGN, CEXPR(CS1(PATH, ivn)), dexpr)));
        }
      } else if (sc) {
        ctor = PN_PUSH(ctor, CS2(ASSIGN, lhs, dexpr != PN_NIL ? dexpr : CS1(VALUE, PN_NIL)));
      } else if (PN_STR_PTR(vn)[0] == '@') {
        ctor = PN_PUSH(ctor, CS2(ASSIGN, lhs, dexpr != PN_NIL ? dexpr : CS1(LIST, PN_NIL)));
      } else {
        PN hinit = CS1(EXPR, PN_PUSH(PN_TUP(CS1(LIST, PN_NIL)), CMSG("table")));
        ctor = PN_PUSH(ctor, CS2(ASSIGN, lhs, dexpr != PN_NIL ? dexpr : hinit));
      }
      if (reader != PN_NIL) {
        PN rbody = CS1(BLOCK, PN_TUP(CEXPR(CS1(PATH, ivn))));
        PN proto = CS2(PROTO, CS1(LIST, PN_NIL), rbody);
        meths = PN_PUSH(meths, CS2(ASSIGN, CS1(EXPR, PN_PUSH(PN_TUP(CS1(MSG, name)), CS1(MSG, reader))),
                                   CEXPR(proto)));
      }
      break;
    }
    case 2: {
      PN mname = PN_TUPLE_AT(it, 1), proto;
      proto = p5_class_method(P, lineno, line, PN_TUPLE_AT(it, 2), PN_TUPLE_AT(it, 3), fields, aliases, pfx);
      meths = PN_PUSH(meths, CS2(ASSIGN, CS1(EXPR, PN_PUSH(PN_TUP(CS1(MSG, name)), CS1(MSG, mname))),
                                 CEXPR(proto)));
      break;
    }
    case 3: {
      PN blk = PN_TUPLE_AT(it, 1);
      PN pre = p5_class_body(P, lineno, line, blk, fields, aliases, PN_TUP0(), pfx);
      ctor = PN_PUSH(ctor, p5_prepend_stmts(P, blk, pre));
      break;
    }
    case 4:
      if (!PN_IS_TUPLE(PN_TUPLE_AT(it, 1))) outer = PN_PUSH(outer, PN_TUPLE_AT(it, 1));
      break;
    }
  }
  p5_cls_put(P, name, p5_tuple_copy(P, sig), p5_tuple_copy(P, ctor));
  cls = CS(MSG, PN_class, CS1(LIST, sig), CS1(BLOCK, ctor));
  /* `Parent class (sig) {..}` derives from Parent, `class (sig) {..}` from Object */
  recv = parent != PN_NIL ? CEXPR(CS1(MSG, parent)) : PN_NIL;
  stmts = outer;
  stmts = PN_PUSH(stmts, CS2(ASSIGN, CEXPR(CS1(MSG, name)),
                             parent != PN_NIL ? CS1(EXPR, PN_PUSH(PN_TUP(CS1(MSG, parent)), cls)) : CEXPR(cls)));
  {
    p5_class_t *rec = p5_class_find(PN_STR_PTR(name), PN_STR_LEN(name));
    PN pars = PN_TUP0();
    int k;
    for (k = 0; rec && k < rec->n; k++)
      pars = PN_PUSH(pars, CEXPR(CS1(VALUE, PN_STRN(rec->par[k], strlen(rec->par[k])))));
    stmts = PN_PUSH(stmts, CEXPR(CS2(MSG, PN_STR("p5classname"), CS1(LIST,
               PN_PUSH(PN_PUSH(PN_TUP(CEXPR(CS1(MSG, name))), CEXPR(CS1(VALUE, name))),
                       CEXPR(CS1(LIST, pars)))))));
  }
  for (i = 0; i < (long)PN_TUPLE_LEN(meths); i++) stmts = PN_PUSH(stmts, PN_TUPLE_AT(meths, i));
  (void)recv;
  return CS1(CODE, stmts);
}

/* the key of the constructor argument K: a literal string, else NIL */
static PN p5_arg_key(PN k) {
  while (PN_PART(k) == AST_EXPR && PN_IS_TUPLE(PN_S(k, 0)) && PN_TUPLE_LEN(PN_S(k, 0)) == 1)
    k = PN_TUPLE_AT(PN_S(k, 0), 0);
  return PN_PART(k) == AST_VALUE && PN_IS_STR(PN_S(k, 0)) ? PN_S(k, 0) : PN_NIL;
}

/* the error text of a class constructor call that cannot be right */
static void p5_class_err(Potion *P, PN node, const char *what, const char *cls, const char *key) {
  char buf[256];
  snprintf(buf, sizeof(buf), "%s \"%s\" constructor%s%s", what, cls, key ? ": " : "", key ? key : "");
  potion_syntax_error(P, PN_SRC(node), "%s", buf);
}

/* `Name->new(k => v, ..)` of a class declared earlier: Name(v1, .., vN) in the order of
 * the :param fields, PN_P5NOARG for the parameters not given. recv is the AST of Name and
 * msg the `new` message. If the argument names are not literal strings, e.g. `Name->new(%h)`,
 * p5new(Name, [args]) matches them at run time. NIL if this is not a call of `new` on a
 * class. */
static PN p5_class_new(Potion *P, long lineno, PN line, PN recv, PN msg) {
  p5_class_t *c;
  PN args = PN_TUP0(), vals[P5_MAXPAR], argl = PN_S(msg, 1), el;
  long i, ne;
  int k, dynamic = 0;
  if (PN_PART(recv) != AST_MSG || !PN_IS_STR(PN_S(recv, 0)) || PN_S(recv, 1) != PN_NIL ||
      strcmp(PN_STR_PTR(PN_S(msg, 0)), "new"))
    return PN_NIL;
  c = p5_class_find(PN_STR_PTR(PN_S(recv, 0)), PN_STR_LEN(PN_S(recv, 0)));
  if (!c) return PN_NIL;
  for (k = 0; k < P5_MAXPAR; k++) vals[k] = PN_NIL;
  el = argl != PN_NIL && PN_S(argl, 0) != PN_NIL ? PN_S(argl, 0) : PN_TUP0();
  ne = PN_TUPLE_LEN(el);
  if (ne % 2) dynamic = 1;
  for (i = 0; !dynamic && i + 1 < ne; i += 2) {
    PN key = p5_arg_key(PN_TUPLE_AT(el, i));
    if (key == PN_NIL) { dynamic = 1; break; }
    for (k = 0; k < c->n; k++)
      if (!strcmp(c->par[k], PN_STR_PTR(key))) break;
    if (k == c->n) p5_class_err(P, msg, "Unrecognised parameters for", c->name, PN_STR_PTR(key));
    vals[k] = PN_TUPLE_AT(el, i + 1);
  }
  if (dynamic) {
    PN cargs = CS1(EXPR, PN_PUSH(PN_TUP(CS1(LIST, el)), CMSG("clone")));
    return CS(MSG, PN_STR("p5new"), CS1(LIST, PN_PUSH(PN_TUP(CEXPR(recv)), cargs)), PN_NIL);
  }
  for (k = 0; k < c->n; k++)  /* all of them: the callee default fill is not relied on */
    args = PN_PUSH(args, vals[k] != PN_NIL ? vals[k]
                   : potion_source(P, AST_VALUE, PN_P5NOARG, PN_NIL, PN_NIL, lineno, line));
  return CS(MSG, PN_S(recv, 0), CS1(LIST, args), PN_NIL);
}

/* the text after __END__/__DATA__ is bound to DATA by a first statement of the program, so that
 * compiled programs have it too */
static PN p5_data_text;
static PN p5_data_stmts(Potion *P, PN s) {
  PN out, call;
  long i;
  if (!p5_data_text) return s;
  call = potion_source(P, AST_EXPR, PN_TUP(potion_source(P, AST_MSG, PN_STR("p5data"),
           potion_source(P, AST_LIST, PN_TUP(potion_source(P, AST_VALUE, p5_data_text, PN_NIL, PN_NIL, 0, PN_NIL)),
                         PN_NIL, PN_NIL, 0, PN_NIL), PN_NIL, 0, PN_NIL)), PN_NIL, PN_NIL, 0, PN_NIL);
  p5_data_text = PN_NIL;
  out = PN_TUP(call);
  if (PN_IS_TUPLE(s)) for (i = 0; i < (long)PN_TUPLE_LEN(s); i++) out = PN_PUSH(out, PN_TUPLE_AT(s, i));
  return out;
}

static int p5_synfail(Potion *P, int line, const char *msg) {
  return potion_send(P->lobby, potion_str(P, "p5synfail"), potion_str(P, msg), PN_NUM(line)) == PN_TRUE;
}

%}

perl5 = vivstart -- repl-mark s:statements end-of-file
   { if (p5_repl) s = p5_local_blockend(P, G->lineno, P->line, s); s = p5_data_stmts(P, s); p5_top_args(P, s); $$ = P->source = PN_AST(CODE, p5_viv_prologue(P, p5_end_append(P, p5_predeclare(P, s))));
     s = (PN)(G->buf+G->pos);
     if (yyleng) YY_ERROR("** Syntax error");
     else if (*(char*)s) YY_ERROR("** Internal parser error: Couldn't parse all statements") }

# AST BLOCK captures lexicals
# Note that if/else blocks (mblock) do not capture lexicals
# block = '{' s:lineseq '}' { $$ = PN_AST(BLOCK, s) }

stmt-line = '' { $$ = PN_NUM(G->lineno) }
statements =
    l1:stmt-line s1:stmt    { $$ = s1 = PN_IS_TUPLE(s1) ? s1 : p5_with_loc(P, G->filename, PN_INT(l1), p5_voidctx(P, s1)) }
        (sep? l2:stmt-line s2:stmt { PN w = p5_with_loc(P, G->filename, PN_INT(l2), p5_voidctx(P, s2)); PN_SIZE wi; for (wi = 0; wi < PN_TUPLE_LEN(w); wi++) s1 = PN_PUSH(s1, PN_TUPLE_AT(w, wi)); $$ = s1 } )* sep?
    | ''              { $$ = PN_NIL }
begin-mark = '' { p5_begin_start(); }
vivstart = '' { p5_viv_begin(P); }

# __END__ / __DATA__: the rest of the file is the handle DATA
dataend = ("__END__" | "__DATA__") !utfw [ \t]* '\r'? '\n'? < .* >
    { if (!p5_data_text) { p5_data_text = PN_NIL; potion_gc_root(P, &p5_data_text); }
      p5_data_text = PN_STRN(yytext, yyleng); $$ = PN_TUP0() }
stmt = dataend
    | STATE '$' n:gid - assign e:eqs - sep?
        { $$ = p5_state_decl(P, G->lineno, P->line, "$", n, p5_unparen(P, e)) }
    | STATE '$' n:gid - sep?
        { $$ = p5_state_decl(P, G->lineno, P->line, "$", n, PN_NIL) }
    | STATE '@' n:gid - sep?
        { $$ = p5_state_decl(P, G->lineno, P->line, "@", n, PN_NIL) }
    | STATE '%' n:gid - sep?
        { $$ = p5_state_decl(P, G->lineno, P->line, "%", n, PN_NIL) }
    | pkgdecl
    | BEGIN begin-mark b:block  { if (p5_begin_end()) $$ = b;
                                  else { p2_eval(P, b); $$ = PN_TUP0(); } }
    | END b:block               { p5_end_add(P, b); $$ = PN_TUP0(); }
    | "CHECK" !utfw - b:block   { p5_check_add(P, b); $$ = PN_TUP0(); }
    | "INIT" !utfw - b:block    { p5_init_add(P, b); $$ = PN_TUP0(); }
    | lb:label s:stmt         { $$ = p5_label_stmt(P, G->lineno, P->line, lb, s) }
    | "require" !utfw - ['"] < [^'"]* > ['"] - sep?
        { $$ = p5_require(P, G->lineno, P->line, yytext, yyleng) }
    | "require" !utfw - "q" "q"? '(' < [^)]* > ')' - sep?
        { $$ = p5_require(P, G->lineno, P->line, yytext, yyleng) }
    | "require" !utfw - modname - sep?  { $$ = PN_TUP0() }   # require Foo::Bar: not loaded
    | SUB n:id - semi -       { $$ = PN_TUP0() }   # forward declaration
    | "..." - &(semi | '}' | !.)  sep?  # the yada yada statement: dies when run
        { $$ = PN_TUP(PN_AST(EXPR, PN_TUP(PN_AST2(MSG, PN_STR("die"), PN_AST(LIST, PN_TUP(p5_str_value(P, PN_STR("Unimplemented")))))))) }
    | subdecl
    | subrout
    | macrodecl
    | classdecl
    | USE "p6" - b:syntax-block --
        { $$ = PN_AST2(MSG, PN_p6, b) }
    | USE "v6" !utfw - b:syntax-block --
        { $$ = PN_AST2(MSG, PN_p6, b) }
    # use integer / no integer: a global flag read by the p5 number methods
    | USE "integer" !utfw - sep?   { $$ = p5_integer_pragma(P, G->lineno, P->line, 1) }
    | NO "integer" !utfw - sep?    { $$ = p5_integer_pragma(P, G->lineno, P->line, 0) }
    | USE "overload" !utfw - l:listexprs - sep?
        { $$ = p5_use_overload(P, G->lineno, P->line, l) }
    | USE "mro" !utfw - l:listexprs - sep?
        { $$ = p5_use_mro(P, G->lineno, P->line, l) }
    | USE ("parent" | "base") !utfw - l:listexprs - sep?
        { $$ = p5_use_parent(P, G->lineno, P->line, l) }
    | u:use &(- (semi | !.)) sep?  { $$ = PN_TUP0() }
    # 'use Foo::Bar LIST;' / 'no Foo qw(..);': import lists are not evaluated yet
    | (USE|NO) modname - (!semi utf8)* sep?  { $$ = PN_TUP0() }
    | i:ifstmt                { $$ = PN_AST(EXPR, i) }
    | "do" !utfw - b:block WHILE e:ifnexpr sep?
      { $$ = p5_do_while(P, G->lineno, P->line, b, e) }
    | "do" !utfw - b:block UNTIL e:ifnexpr sep?
      { $$ = p5_do_while(P, G->lineno, P->line, b, PN_AST(NOT, PN_AST(EXPR, PN_TUPIF(e)))) }
    # our $x / @a / %h: a package variable is a plain lobby global here
    | OUR { p5_in_our = 1; } l:listvar &(- (semi | '}' | !.)) sep?
        { p5_in_our = 0; $$ = p5_our_init(P, G->lineno, P->line, l, 0) }
    | OUR { p5_in_our = 1; } l:hashvar &(- (semi | '}' | !.)) sep?
        { p5_in_our = 0; $$ = p5_our_init(P, G->lineno, P->line, l, 1) }
    | OUR { p5_in_our = 1; } s:stmt                 { p5_in_our = 0; $$ = s }
    # local @a / %h [= LIST]: the statement is the normal list assignment, the saved name is its target
    | LOCAL &[@%] lv:hashvar - &(semi | '}' | !.) sep?
        { p5_local_note_of(P, lv); $$ = p5_local_empty(P, G->lineno, P->line, lv) }
    | LOCAL &[@%] lv:listvar - &(semi | '}' | !.) sep?
        { p5_local_note_of(P, lv); $$ = p5_local_empty(P, G->lineno, P->line, lv) }
    | LOCAL &[@%] s:stmt
        { p5_local_note_target(P, s); $$ = s }
    | LOCAL l:list assign r:listrhs - sep?
        { p5_local_note_list(P, l); $$ = p5_aasign(P, G->lineno, P->line, l, r) }
    | LOCAL l:list assign e:eqs - sep?
        { p5_local_note_list(P, l); $$ = p5_aasign(P, G->lineno, P->line, l, PN_AST(LIST, PN_TUP(p5_listctx(P, e)))) }
    | LOCAL l:scalar assign e:eqs - IF c:ifnexpr sep?
        { p5_local_note_of(P, l); $$ = PN_OP(AST_AND, c, PN_AST2(ASSIGN, l, p5_each_scalar(P, G->lineno, P->line, p5_unparen(P, e)))) }
    | LOCAL l:scalar assign e:eqs - UNLESS c:ifnexpr sep?
        { p5_local_note_of(P, l); $$ = PN_OP(AST_AND, PN_AST(NOT, c), PN_AST2(ASSIGN, l, p5_each_scalar(P, G->lineno, P->line, p5_unparen(P, e)))) }
    | LOCAL l:scalar assign e:eqs - sep?
        { p5_local_note_of(P, l); $$ = PN_AST2(ASSIGN, l, p5_each_scalar(P, G->lineno, P->line, p5_unparen(P, e))) }
    | LOCAL l:scalar - sep?
        { p5_local_note_of(P, l); $$ = PN_AST2(ASSIGN, l, PN_AST(VALUE, PN_NIL)) }
    # local $h{k} = v / local $a[i] = v: the element is saved and restored at the end of the
    # block (a key that did not exist is deleted again)
    | LOCAL l:hashel assign e:sets - sep?
        { p5_local_note(P, l); $$ = p5_fix_assign(P, G->lineno, P->line, PN_AST2(ASSIGN, l, p5_unparen(P, e))) }
    | LOCAL l:listel assign e:sets - sep?
        { p5_local_note(P, l); $$ = p5_fix_assign(P, G->lineno, P->line, PN_AST2(ASSIGN, l, p5_unparen(P, e))) }
    | LOCAL l:hashel - sep?
        { p5_local_note(P, l); $$ = p5_fix_assign(P, G->lineno, P->line, PN_AST2(ASSIGN, l, PN_AST(VALUE, PN_NIL))) }
    | LOCAL l:listel - sep?
        { p5_local_note(P, l); $$ = p5_fix_assign(P, G->lineno, P->line, PN_AST2(ASSIGN, l, PN_AST(VALUE, PN_NIL))) }
    # loop ... continue BLOCK: while runs the block at the loop test (next jumps there), foreach after the body
    | "while" !utfw - list-start - r:readline - list-end b:block - "continue" !utfw - cb:block sep?
        { $$ = p5_while_rl_cont(P, G->lineno, P->line, PN_AST(MSG, PN_STR("$_")), r, b, cb) }
    | "while" !utfw - list-start - MY? v:scalar - assign - r:readline - list-end b:block - "continue" !utfw - cb:block sep?
        { $$ = p5_while_rl_cont(P, G->lineno, P->line, v, r, b, cb) }
    | "while" !utfw - c:list - b:block - "continue" !utfw - cb:block sep?
        { $$ = p5_while_cont(P, G->lineno, P->line, c, b, cb, 0) }
    | UNTIL c:list b:block - "continue" !utfw - cb:block sep?
        { $$ = p5_while_cont(P, G->lineno, P->line, c, b, cb, 1) }
    | (FOR | FOREACH) i:lexglobalf l:list b:block - "continue" !utfw - cb:block sep?
        { $$ = p5_forlist(P, G->lineno, P->line, i, l, p5_block_cat(P, G->lineno, P->line, b, cb)) }
    | (FOR | FOREACH) l:list b:block - "continue" !utfw - cb:block sep?
        { $$ = p5_forlist(P, G->lineno, P->line, PN_AST(MSG, PN_STR("$_")), l, p5_block_cat(P, G->lineno, P->line, b, cb)) }
    | cforstmt
    | (FOR | FOREACH) l:list b:block     # for (LIST) {...}: the loop variable is $_
      { $$ = p5_forlist(P, G->lineno, P->line, PN_AST(MSG, PN_STR("$_")), l, b) }
    | forlist
    | "while" !utfw - list-start - r:readline - list-end b:block sep?
        { $$ = p5_while_rl(P, G->lineno, P->line, PN_AST(MSG, PN_STR("$_")), r, b) }
    | "while" !utfw - list-start - c:mylassign - list-end b:block sep?
        { $$ = p5_special_stmt(P, G->lineno, PN_AST(MSG, PN_STR("while")), PN_AST(LIST, PN_TUP(c)), b) }
    | "while" !utfw - list-start - l:list assign e:eqs - list-end b:block sep?     # while (($k, $v) = each %h)
        { PN cnd = p5_aasign_count(P, G->lineno, P->line, l, PN_AST(LIST, PN_TUP(p5_listctx(P, e))));
          $$ = p5_special_stmt(P, G->lineno, PN_AST(MSG, PN_STR("while")), PN_AST(LIST, PN_TUP(cnd)), b) }
    | "while" !utfw - list-start - MY? v:scalar - assign - r:readline - list-end b:block sep?
        { $$ = p5_while_rl(P, G->lineno, P->line, v, r, b) }
    # 'while (...) {...}' is a complete statement: without this, a following
    # 'if (...)' on the next line was taken as its statement modifier.
    | m:special l:list b:block sep?
        { $$ = p5_special_stmt(P, G->lineno, m, p5_special_cond(P, G->lineno, P->line, m, l), b) }
    | UNTIL l:list b:block sep?      # until (COND) BLOCK: while (!COND) BLOCK
        { PN mm = PN_AST(MSG, PN_while);
          $$ = p5_special_stmt(P, G->lineno, mm, p5_until_cond(P, G->lineno, P->line, p5_special_cond(P, G->lineno, P->line, mm, l)), b) }
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
    | a:assigndecl - ',' - b:sets sep?    # $x = 1, "\n"; the comma operator after an assignment
      { $$ = PN_AST(EXPR, PN_PUSH(PN_TUP(a), b)) }
    | assigndecl sep?
    | b:block ( - "continue" !utfw - cb:block sep? { $$ = p5_bare_cont(P, G->lineno, P->line, b, cb) }
              | '' { $$ = p5_bare_block(P, G->lineno, P->line, b) } )
    | a:sets WHILE e:ifnexpr sep?
      { $$ = p5_while_mod(P, G->lineno, P->line, e, a) }
    # push (@a, $x), push (@b, 1) while COND: a comma statement with a modifier
    | a:sets comma b:sets (comma b2:sets { b = PN_PUSH(PN_IS_TUPLE(b) ? b : PN_TUP(b), b2) })*
      WHILE e:ifnexpr sep?
      { PN body = PN_AST(EXPR, PN_PUSH(PN_TUP(a), PN_IS_TUPLE(b) ? b : PN_TUP(b)));
        $$ = p5_while_mod(P, G->lineno, P->line, e, body) }
    | a:sets UNTIL e:ifnexpr sep?
      { $$ = p5_while_mod(P, G->lineno, P->line, PN_AST(NOT, PN_AST(EXPR, PN_TUPIF(e))), a) }
    | a:sets (FOR | FOREACH) l:formod-list sep?
      { $$ = p5_forlist(P, G->lineno, P->line, PN_AST(MSG, PN_STR("$_")), l,
                        PN_AST(BLOCK, PN_TUP(a))) }
    | a:sets IF e:ifnexpr sep?
      { $$ = PN_OP(AST_AND, e, a) }
    | a:sets UNLESS e:ifnexpr sep?
      { $$ = PN_OP(AST_AND, PN_AST(NOT, e), a) }
    | &{ p5_lb_depth > 0 } k:sets { $$ = k = PN_TUP(k) } ( - (comma | fatcomma) - b:sets { $$ = k = PN_PUSH(k, b) } )+ sep? &(- '}')   # { $_ => 1 } of map/grep/sort: the list is the value
        { $$ = PN_AST(EXPR, PN_TUP(PN_AST(LIST, k))) }
    | k:sets { $$ = k = PN_TUP(k) } ( - comma - b:sets { $$ = k = PN_PUSH(k, b) } )+ sep { $$ = PN_AST(EXPR, k) }   # a, b;: the comma operator at statement level
    | e:sets - comma - &('}' | !.) { $$ = e }   # `f(),` as the last statement of a block
    | e:sets sep { $$ = p5_want_void(P, G->lineno, P->line, e) }   # `f();`: void context (only when the source mentions wantarray)
    | s:sets
        ( or x:sets           { s = PN_OP(AST_OR, p5_unparen(P, s), p5_unparen(P, x)) }
        | and x:sets          { s = PN_OP(AST_AND, p5_unparen(P, s), p5_unparen(P, x)) })* sep?
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
myassign = MY i:global assign e:eqs -  { $$ = PN_AST2(ASSIGN, i, p5_each_scalar(P, G->lineno, P->line, p5_unparen(P, e))) }
mylassign = MY l:list assign r:cond-rhs -  { $$ = p5_aasign_count(P, G->lineno, P->line, l, r) }
cond-rhs = r:listrhs &(- ')') { $$ = r }
         | e:eqs             { $$ = PN_AST(LIST, PN_TUP(e)) }
listitem = fatkey | mylassign | myassign | rangeset
callitem = fatkey | rangeset
# a..b in list context: p5range(a, b) returns the tuple of integers
range = a:eqs - ( "..." | ".." !'.' ) - b:eqs
          { $$ = PN_AST(EXPR, PN_TUP(PN_AST2(MSG, PN_STR("p5range"),
                    PN_AST(LIST, PN_PUSH(PN_TUP(a), b))))) }

# a..b or any other term: parsed once (trying `range` and then `sets` parsed the term twice, exponential in the nesting of anonymous subs)
rangeonly = a:sets - ( "..." | ".." !'.' ) - b:eqs
          { $$ = PN_AST(EXPR, PN_TUP(PN_AST2(MSG, PN_STR("p5range"), PN_AST(LIST, PN_PUSH(PN_TUP(a), b))))) }
rangeset = a:sets ( - ( "..." | ".." !'.' ) - b:eqs
          { a = PN_AST(EXPR, PN_TUP(PN_AST2(MSG, PN_STR("p5range"), PN_AST(LIST, PN_PUSH(PN_TUP(a), b))))) } )? { $$ = a }

listexprs = e1:listitem      { $$ = e1 = PN_IS_TUPLE(e1) ? e1 : PN_TUP(e1) }
        ( - (comma|fatcomma) ( - comma )* - e2:listitem   { $$ = e1 = PN_PUSH(e1, e2) } )*
        ( - comma )?   # trailing comma
# listexprs + named args: $x=1 (i.e. assignment)
callexprs = e1:callitem      { $$ = e1 = PN_IS_TUPLE(e1) ? e1 : PN_TUP(e1) }
        ( - (comma|fatcomma) - e2:callitem   { $$ = e1 = PN_PUSH(e1, e2) } )*
        ( - comma )?   # trailing comma

BEGIN   = "BEGIN" space+
PACKAGE = "package" space+
USE     = "use" space+
NO      = "no" space+
SUB     = "sub" (space+ | &'{')
MACRO   = "macro" space+
IF      = "if" !utfw -
UNLESS  = "unless" !utfw -
WHILE   = "while" !utfw -
UNTIL   = "until" !utfw -
ELSIF   = "elsif" !utfw -
ELSE    = "else" !utfw -
MY      = "my" !utfw space* ( &'(' { p5_in_my = 2; } | '' { p5_in_my = 1; } )   # see p5_vn: the session variables of p2 -d
MYFOR   = "my" !utfw space* { p5_in_my = 3; }
LOCAL   = "local" !utfw space*
STATE   = "state" !utfw space*
OUR     = "our" !utfw space*
END     = "END" !utfw -
FOR     = "for" !utfw -
FOREACH = "foreach" !utfw -
RETURN  = "return" !utfw -

p5-siglist = list-start args2* list-end { $$ = PN_AST(LIST, P->source); P->source = PN_NIL }
#TODO: store name globally
# sub f ($$;@) {...}: a prototype is only sigils, so it is parsed and ignored
p5proto = '(' [$@%&*;\\ \t_+]* ')' -
# sub attributes (:lvalue, :method, :Name(args)) are parsed and ignored
subattrs = ( ':' - !':' [A-Za-z_] [A-Za-z0-9_]* ( '(' [^)]* ')' )? - )*
# a forward declaration: sub name; / sub name($$);
subdecl = SUB qid - ( p5proto )? subattrs ';' - { $$ = PN_TUP0() }
subrout = SUB n:qid { p5_set_cursub(n); } - p5proto subattrs b:block
          { $$ = p5_sub_def(P, G->lineno, P->line, n, p5_sub_proto(P, G->lineno, P->line, b)) }
        | SUB n:qid { p5_set_cursub(n); } - l:p5-siglist subattrs b:block
          { $$ = p5_sub_def(P, G->lineno, P->line, n, PN_AST2(PROTO, l, b)) }
        | SUB n:qid { p5_set_cursub(n); } - subattrs b:block
          { $$ = p5_sub_def(P, G->lineno, P->line, n, p5_sub_proto(P, G->lineno, P->line, b)) }
# macro NAME [:infix|:prefix|:postfix|:circumfix|:postcircumfix] [(sig)] BLOCK; NAME may be a
# quoted operator. The optional parts have explicit empty alternatives (stale greg slots).
macroname = !keyword m:qid { $$ = m }
          | ['] < [^']+ > ['] { $$ = PN_STRN(yytext, yyleng) }
macroattr = ':' < ( "postcircumfix" | "circumfix" | "infix" | "prefix" | "postfix" ) > !utfw -
              { $$ = PN_STRN(yytext, yyleng) }
          | ''  { $$ = PN_NIL }
macrosig = l:p5-siglist - { $$ = l }
         | ''             { $$ = PN_NIL }
macrodecl = MACRO n:macroname - a:macroattr l:macrosig b:block sep?
        { $$ = p5_macro_def(P, G->lineno, P->line, n, a, l, b) }
# quasi BLOCK [:COMPILING]: the code of the block as a closure; `STATEMENTS` inside is an unquote
# splice, replaced by the macro expander with the code that the statements evaluate to.
quasi = "quasi" !utfw - (':' "COMPILING" !utfw -)? &{ p5_qd(1) } b:block &{ p5_qd(-1) }
        { $$ = p5_quasi(P, G->lineno, P->line, b) }
unquote = '`' s:statements '`' -
        { $$ = PN_AST(EXPR, PN_TUP(PN_AST3(MSG, PN_STR("p5unquote"), PN_NIL, PN_AST(BLOCK, s)))) }
# no optional 'l:p5-siglist?' here: when the siglist is absent greg leaves
# the previous anonsub's stale 'l' in the slot (segfault in sig_compile).
anonsub = SUB p5proto subattrs b:block
        { $$ = p5_sub_proto(P, G->lineno, P->line, b) }
        | SUB l:p5-siglist b:block
        { $$ = PN_AST2(PROTO, l, b) }
        | SUB subattrs b:block
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

labelname = < [A-Z_] [A-Z0-9_]* > !utfw { $$ = PN_STRN(yytext, yyleng) }
# a lower case label (`line: while (..)`, `last line;`): not a statement modifier word
lcname = labelname | lclabel
lclabel = !(("if" | "unless" | "while" | "until" | "for" | "foreach" | "and" | "or" | "xor" | "x") !utfw) < [a-z_] [a-z0-9_]* > !utfw { $$ = PN_STRN(yytext, yyleng) }
label = n:labelname - ':' !':' - { $$ = n }
      | n:lclabel - ':' !':' - &(("while" | "until" | "for" | "foreach" | '{') ) { $$ = n }
      | n:lclabel - ':' !':' - &([a-zA-Z$@%&\\{;]) { $$ = n }   # label1: any statement
# last/next map to potion's break/continue; last LABEL / next LABEL carry the label
# as an argument and leave the enclosing loop labeled LABEL (core/compile.c).
loopctl = "last" !utfw - n:lcname -  { $$ = p5_loopctl_label(P, G->lineno, P->line, PN_break, n) }
        | "next" !utfw - n:lcname -  { $$ = p5_loopctl_label(P, G->lineno, P->line, PN_continue, n) }
        | "last" !utfw -                { $$ = PN_AST(MSG, PN_break) }
        | "next" !utfw -                { $$ = PN_AST(MSG, PN_continue) }
modname = < utfw+ ('::' utfw+)* >
pkgname = < utfw+ ('::' utfw+)* > -  { $$ = PN_STRN(yytext, yyleng) }
# perl 5.38 class feature: class NAME [VERSION] [:isa(PARENT)] { ... } or `class NAME;`
# up to the next class/package. Items: field, method, ADJUST, any other statement.
CLASS  = "class" space+
FIELD  = "field" space+
METHOD = "method" space+
classhead = CLASS n:qid - (version -)? ':' - "isa" !utfw - '(' - p:qid - (version -)? ')' -
              { p5_class_begin(n, p); $$ = PN_PUSH(PN_TUP(n), p) }
          | CLASS n:qid -  (version -)?
              { p5_class_begin(n, PN_NIL); $$ = PN_PUSH(PN_TUP(n), PN_NIL) }
classdecl = h:classhead - '{' - ci:classitems - '}' -
              { $$ = p5_class_end(P, G->lineno, P->line, h, ci) }
          | h:classhead - semi -- ci:classstmtitems
              { $$ = p5_class_end(P, G->lineno, P->line, h, ci) }
classitems = i1:classitem { $$ = i1 = PN_TUP(i1) }
               ( -- i2:classitem { $$ = i1 = PN_PUSH(i1, i2) } )* --
           | -- { $$ = PN_NIL }
classstmtitems = !CLASS !PACKAGE i1:classitem { $$ = i1 = PN_TUP(i1) }
               ( -- !CLASS !PACKAGE i2:classitem { $$ = i1 = PN_PUSH(i1, i2) } )* --
           | -- { $$ = PN_NIL }
classitem = fielddecl
          | METHOD n:qid - l:p5-siglist b:block
              { $$ = PN_PUSH(PN_PUSH(PN_PUSH(PN_TUP(PN_NUM(2)), n), l), b) }
          | METHOD n:qid - b:block
              { $$ = PN_PUSH(PN_PUSH(PN_PUSH(PN_TUP(PN_NUM(2)), n), PN_NIL), b) }
          | METHOD n:qid - semi    # forward declaration
              { $$ = PN_PUSH(PN_TUP(PN_NUM(4)), PN_TUP0()) }
          | "ADJUST" !utfw - b:block   { $$ = PN_PUSH(PN_TUP(PN_NUM(3)), b) }
          | s:stmt                 { $$ = PN_PUSH(PN_TUP(PN_NUM(4)), s) }
fieldattr = ':' - "param" !utfw - '(' - n:id - ')' -  { $$ = PN_PUSH(PN_TUP(PN_STR("param")), n) }
          | ':' - "param" !utfw -                     { $$ = PN_PUSH(PN_TUP(PN_STR("param")), PN_NIL) }
          | ':' - "reader" !utfw - '(' - n:id - ')' - { $$ = PN_PUSH(PN_TUP(PN_STR("reader")), n) }
          | ':' - "reader" !utfw -                    { $$ = PN_PUSH(PN_TUP(PN_STR("reader")), PN_NIL) }
fieldattrs = a:fieldattr { $$ = a = PN_TUP(a) } ( a2:fieldattr { $$ = a = PN_PUSH(a, a2) } )*
           | '' { $$ = PN_NIL }
fielddef-s = "//=" - e:eqs -   { $$ = PN_PUSH(PN_TUP(PN_NUM(2)), p5_unparen(P, e)) }
           | "||=" - e:eqs -   { $$ = PN_PUSH(PN_TUP(PN_NUM(3)), p5_unparen(P, e)) }
           | assign e:eqs -    { $$ = PN_PUSH(PN_TUP(PN_NUM(1)), p5_unparen(P, e)) }
           | '' { $$ = PN_PUSH(PN_TUP(PN_NUM(0)), PN_NIL) }
fielddef-l = assign r:list -   { PN f = p5_flatten_multi(P, G->lineno, P->line, r);
                                 $$ = PN_PUSH(PN_TUP(PN_NUM(1)), f != r ? f : p5_flatten1(P, r)) }
           | '' { $$ = PN_PUSH(PN_TUP(PN_NUM(0)), PN_NIL) }
fielddef-h = assign r:list -   { $$ = PN_PUSH(PN_TUP(PN_NUM(1)),
                                   PN_AST(EXPR, PN_PUSH(PN_TUP(p5_flatten1(P, r)), PN_AST(MSG, PN_STR("table"))))) }
           | '' { $$ = PN_PUSH(PN_TUP(PN_NUM(0)), PN_NIL) }
fielddecl = FIELD v:plainscalar a:fieldattrs d:fielddef-s &(- (semi | '}' | !.)) sep?
              { $$ = p5_class_field_item(P, v, a, (int)PN_INT(PN_TUPLE_AT(d, 0)), PN_TUPLE_AT(d, 1)) }
          | FIELD v:listvar a:fieldattrs d:fielddef-l &(- (semi | '}' | !.)) sep?
              { $$ = p5_class_field_item(P, v, a, (int)PN_INT(PN_TUPLE_AT(d, 0)), PN_TUPLE_AT(d, 1)) }
          | FIELD v:hashvar a:fieldattrs d:fielddef-h &(- (semi | '}' | !.)) sep?
              { $$ = p5_class_field_item(P, v, a, (int)PN_INT(PN_TUPLE_AT(d, 0)), PN_TUPLE_AT(d, 1)) }
pkgdecl = PACKAGE n:pkgname v:version? sep
          { p5_set_pkg(n); $$ = PN_AST(EXPR, PN_TUP(PN_AST2(MSG, PN_STR("p5pkg"), PN_AST(LIST, PN_TUP(PN_AST(VALUE, n)))))) }
        | PACKAGE n:pkgname v:version? { p5_pkg_save(); p5_set_pkg(n); } b:block
          { p5_pkg_restore(); $$ = p5_pkg_block(P, G->lineno, P->line, n, b) }

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
ifexpr = list-start e:ifexpr0 - list-end { $$ = p5_cond(P, G->lineno, P->line, e) }
ifexpr0 = mylassign | myassign | sets
# '(COND)' ends the condition only at the end of the statement: '(a || b) == 0' is one expression
ifnexpr = ifexpr &(- (semi | '}' | !. | ("if" | "unless" | "for" | "foreach" | "while" | "until") !utfw))
        | a:sets &(- (semi | '}' | !. | ("if" | "unless" | "for" | "foreach" | "while" | "until") !utfw)) { $$ = p5_cond(P, G->lineno, P->line, a) }
        | e:eqs { $$ = p5_cond(P, G->lineno, P->line, e) }

# the empty alternatives return NIL explicitly: an optional 'x:rule?' leaves
# the previous match's stale value in greg's slot when it does not match
# 'STMT for LIST;' list part: a parenthesized list, a range, or one expression
formod-list = l:list { $$ = l }
            | x:range { $$ = PN_AST(LIST, PN_TUP(x)) }
            | x1:eqs { $$ = x1 = PN_TUP(x1) } ( - comma - x2:eqs { $$ = x1 = PN_PUSH(x1, x2) } )+ { $$ = PN_AST(LIST, x1) }
            | x:eqs { $$ = PN_AST(LIST, PN_TUP(x)) }
cfor-init = assigndecl | sets | '' { $$ = PN_NIL }
cfor-cond = MY? l:list assign r:listrhs &(- semi) { $$ = p5_aasign_count(P, G->lineno, P->line, l, r) }
          | MY? l:list assign e:eqs &(- semi) { $$ = p5_aasign_count(P, G->lineno, P->line, l, PN_AST(LIST, PN_TUP(p5_listctx(P, e)))) }   # for (;($k, $v) = each %h;)
          | eqs | '' { $$ = PN_NIL }
cfor-step = sets | '' { $$ = PN_NIL }
cforstmt = (FOR | FOREACH) list-start - i:cfor-init - semi - c:cfor-cond - semi - s:cfor-step - list-end b:block
            { $$ = p5_cfor(P, G->lineno, P->line, i, c, s, b) }
forlist = (FOR | FOREACH) i:lexglobalf l:list b:block
            { $$ = p5_forlist(P, G->lineno, P->line, i, l, b) }

returnstmt = RETURN !(("if" | "unless" | "for" | "foreach" | "while" | "until") !utfw) e:sets -
               { PN m = PN_AST(MSG, PN_return);
                 PN_SRC(m)->a[1] = PN_SRC(PN_AST(LIST, PN_TUP(p5_unparen(P, e)))); PN_TOUCH(m);
                 $$ = PN_AST(EXPR, PN_TUP(m)) }
           | "goto" !utfw - '&' i:id - &(semi | '}' | !. | IF | UNLESS)   # goto &sub: the sub is called with the current @_ and its value returned
               { PN m = PN_AST(MSG, PN_return), call = PN_PUSH(PN_TUP(p5_qualify_call(P, PN_AST(MSG, i))),
                     PN_AST2(MSG, PN_STR("p5callv"), PN_AST(LIST, PN_TUP(PN_AST(EXPR, PN_TUP(PN_AST(MSG, PN_STR("@_"))))))));
                 PN_SRC(m)->a[1] = PN_SRC(PN_AST(LIST, PN_TUP(PN_AST(EXPR, call)))); PN_TOUCH(m);
                 $$ = PN_AST(EXPR, PN_TUP(m)) }
           | "goto" !utfw - '&' s:plainscalar - &(semi | '}' | !. | IF | UNLESS)
               { PN m = PN_AST(MSG, PN_return), call = PN_PUSH(PN_TUP(s),
                     PN_AST2(MSG, PN_STR("p5callv"), PN_AST(LIST, PN_TUP(PN_AST(EXPR, PN_TUP(PN_AST(MSG, PN_STR("@_"))))))));
                 PN_SRC(m)->a[1] = PN_SRC(PN_AST(LIST, PN_TUP(PN_AST(EXPR, call)))); PN_TOUCH(m);
                 $$ = PN_AST(EXPR, PN_TUP(m)) }
           | RETURN -
               { $$ = PN_AST(EXPR, PN_TUP(PN_AST(MSG, PN_return))) }

assigndecl = a:assigndecl0 { $$ = p5_tie_aggr(P, G->lineno, P->line, a) }
assigndecl0 =
        # 'my @a;' / 'my %h;' start out as an empty array / hash, not undef
        MY l:listvar &(- (semi | '}' | !.))
          { $$ = p5_top_decl(P, l) ? PN_AST(EXPR, PN_TUP(PN_AST(VALUE, PN_NIL))) : PN_AST2(ASSIGN, l, PN_AST(LIST, PN_NIL)) }
      | MY l:hashvar &(- (semi | '}' | !.))
          { if (p5_top_decl(P, l)) $$ = PN_AST(EXPR, PN_TUP(PN_AST(VALUE, PN_NIL))); else $$ = PN_AST2(ASSIGN, l, PN_AST(EXPR, PN_PUSH(PN_TUP(PN_AST(LIST, PN_NIL)),
                                                         PN_AST(MSG, PN_STR("table"))))) }
      | MY l:scalar &(- (semi | '}' | !.))   # my $x; a real variable (undef), so that closures and eval blocks capture it
          { $$ = PN_AST2(ASSIGN, l, PN_AST(VALUE, PN_NIL)) }
      | MY l:list &(- (semi | '}' | !.))   # my ($a, @b, %c); the arrays and hashes start out empty
          { PN s1 = PN_TUP0(); PN_TUPLE_EACH(PN_S(l,0), i, v, {
              PN m = v;
              const char *nm;
              if (PN_PART(m) == AST_EXPR && PN_IS_TUPLE(PN_S(m,0)) && PN_TUPLE_LEN(PN_S(m,0)) == 1)
                m = PN_TUPLE_AT(PN_S(m,0), 0);
              nm = PN_PART(m) == AST_MSG && PN_IS_STR(PN_S(m,0)) ? PN_STR_PTR(PN_S(m,0)) : "";
              PN init = nm[0] == '@' ? PN_AST(LIST, PN_NIL)
                      : nm[0] == '%' ? PN_AST(EXPR, PN_PUSH(PN_TUP(PN_AST(LIST, PN_NIL)), PN_AST(MSG, PN_STR("table"))))
                      : PN_AST(VALUE, PN_NIL);
              s1 = PN_PUSH(s1, PN_AST2(ASSIGN, v, init));
            }); $$ = PN_AST(EXPR, s1) }
      |         MY t:name l:listvar assign r:list { PN_SRC(l)->a[2] = PN_SRC(t); PN_TOUCH(l); $$ = PN_AST2(ASSIGN, l, r) }
      | MY? l:listvar assign x:rangeonly -    # my @a = 1..3
          { $$ = PN_AST2(ASSIGN, l, x) }
      | MY? l:hashvar assign x:rangeonly -    # my %h = 1..4
          { $$ = PN_AST2(ASSIGN, l, PN_AST(EXPR, PN_PUSH(PN_TUP(x), PN_AST(MSG, PN_STR("table"))))) }
      | MY? l:listvar assign r:list !(- '[') rep c:eqs   # my @a = (LIST) x N
          { $$ = PN_AST2(ASSIGN, l, PN_AST(EXPR, PN_PUSH(PN_TUP(p5_listlit_array(P, r)),
                   PN_AST2(MSG, PN_STR("p5lrepeat"), PN_AST(LIST, PN_TUP(c)))))) }
      | MY? l:listvar assign '(' - l2:listvar assign r2:list - ')' -   # @x = (@y = LIST): the assignment's value is @y
          { PN f = p5_flatten_multi(P, G->lineno, P->line, r2);
            PN inner = PN_AST2(ASSIGN, l2, f != r2 ? f : p5_flatten1(P, r2));
            $$ = PN_AST2(ASSIGN, l, PN_AST(EXPR, PN_PUSH(PN_TUP(inner), PN_AST(MSG, PN_STR("clone"))))) }
      | MY? l:listvar assign '(' - l2:listvar assign e2:eqs - ')' -   # @x = (@y = split(...))
          { $$ = PN_AST2(ASSIGN, l, PN_AST(EXPR, PN_PUSH(PN_TUP(PN_AST2(ASSIGN, l2, e2)), PN_AST(MSG, PN_STR("clone"))))) }
      | p:slicepair assign r:list !(- '[')           # @a[1,2] = LIST; @h{qw(a b)} = LIST
          { PN f = p5_flatten_multi(P, G->lineno, P->line, r); PN rhs = f != r ? f : p5_flatten1(P, r);
            $$ = p5_slice_op(P, G->lineno, P->line, PN_TUPLE_AT(p, 0), PN_TUPLE_AT(p, 1), rhs) }
      | MY? l:listvar assign r:list !(- '[')       { PN f = p5_flatten_multi(P, G->lineno, P->line, r); PN rhs = p5_copy_rhs(P, l, f != r ? f : p5_flatten1(P, r)); $$ = p5_isa_assign(P, G->lineno, P->line, l, rhs, PN_AST2(ASSIGN, l, rhs)) }
      | MY? l:hashvar assign r:list !(- '[')
          { PN m = PN_AST(MSG, PN_STR("table"));
            PN fm = p5_flatten_multi(P, G->lineno, P->line, r);
            PN call = PN_AST(EXPR, PN_PUSH(PN_TUP(fm != r ? fm : p5_flatten1(P, r)), m));
            $$ = PN_AST2(ASSIGN, l, call) }
      | MY? l:hashvar assign e:eqs      # %h = @_ / %h = f(): the list's pairs
          { $$ = PN_AST2(ASSIGN, l, PN_AST(EXPR, PN_PUSH(PN_TUPIF(e), PN_AST(MSG, PN_STR("table"))))) }
      | MY t:name l:list assign r:list    # typed lists
          { PN s1 = PN_TUP0(); PN_TUPLE_EACH(PN_S(l,0), i, v, {
            PN_SRC(v)->a[2] = PN_SRC(t); PN_TOUCH(v);
            s1 = PN_PUSH(s1, PN_AST2(ASSIGN, v, potion_tuple_at(P,0,PN_S(r,0),PN_NUM(i))));
          }); $$ = PN_AST(EXPR, s1) }
      | MY? l:list assign MY? l2:list assign r:listrhs       # ($a, $b) = ($b, $a) = LIST: the inner assignment first
          { PN in = p5_aasign(P, G->lineno, P->line, l2, r);
            PN out = p5_aasign(P, G->lineno, P->line, l, l2);
            $$ = PN_AST(EXPR, PN_PUSH(PN_TUP(in), out)) }
      | MY? l:list assign r:listrhs !(- '[')       # aasign
          { $$ = p5_aasign(P, G->lineno, P->line, l, r) }
      | MY? l:list assign e:eqs -          # my ($a, $b) = split ...; = f(); = "str"
          { $$ = p5_aasign(P, G->lineno, P->line, l, PN_AST(LIST, PN_TUP(p5_listctx(P, e)))) }
      | ("local" !utfw -)? '*' n:qid - assign '*' gs:qid -          # *a = *b: every slot
          { $$ = PN_AST(EXPR, PN_TUP(p5_globset_ast(P, G->lineno, P->line, p5_str_value(P, p5_qname(P, n)), PN_STR(""),
                   p5_glob_ast(P, G->lineno, P->line, p5_str_value(P, p5_qname(P, gs)), PN_STR(""))))) }
      | ("local" !utfw -)? '*' n:qid - assign &(SUB | '\\' '&') e:eqs -   # *name = sub {..} / \&f: defines the sub
          { $$ = PN_AST(EXPR, PN_TUP(p5_sub_def(P, G->lineno, P->line, n, p5_unparen(P, e)))) }
      | ("local" !utfw -)? '*' n:qid - assign e:eqs -               # *name = \@a / \%h / \&f ... : sets that slot
          { $$ = PN_AST(EXPR, PN_TUP(p5_globset_ast(P, G->lineno, P->line, p5_str_value(P, p5_qname(P, n)), PN_STR(""), p5_unparen(P, e)))) }
      | ("local" !utfw -)? '*{' - gk:eqs - '}' - assign '*' gs:qid -     # *{"name"} = *other
          { $$ = PN_AST(EXPR, PN_TUP(p5_globset_ast(P, G->lineno, P->line, p5_unparen(P, gk), potion_str(P, p5_pkg[0] ? p5_pkg : "main"),
                   p5_glob_ast(P, G->lineno, P->line, p5_str_value(P, p5_qname(P, gs)), PN_STR(""))))) }
      | ("local" !utfw -)? '*{' - gk:eqs - '}' - assign e:eqs -     # *{"Pkg::name"} = ..., the name is computed
          { $$ = PN_AST(EXPR, PN_TUP(p5_globset_ast(P, G->lineno, P->line, p5_unparen(P, gk), potion_str(P, p5_pkg[0] ? p5_pkg : "main"), p5_unparen(P, e)))) }
      | '$#{' - r:eqs - '}' - assign e:eqs -   # $#{$r} = N / $#$r = N: resize the array REF points to
          { $$ = PN_AST(EXPR, PN_PUSH(PN_TUP(r), PN_AST2(MSG, PN_STR("p5setlast"),
                   PN_AST(LIST, PN_TUP(PN_OP(AST_PLUS, p5_unparen(P, e), PN_AST(VALUE, PN_NUM(1)))))))) }
      | '$#' r:plainscalar - assign e:eqs -
          { $$ = PN_AST(EXPR, PN_PUSH(PN_TUP(r), PN_AST2(MSG, PN_STR("p5setlast"),
                   PN_AST(LIST, PN_TUP(PN_OP(AST_PLUS, p5_unparen(P, e), PN_AST(VALUE, PN_NUM(1)))))))) }
      | "pos" !utfw - list-start - sv:plainscalar - list-end - assign e:eqs -
          { $$ = p5_pos_assign(P, G->lineno, P->line, sv, p5_unparen(P, e)) }
      | "pos" !utfw - sv:plainscalar - assign e:eqs -
          { $$ = p5_pos_assign(P, G->lineno, P->line, sv, p5_unparen(P, e)) }
      | "pos" !utfw - list-start - list-end - assign e:eqs -
          { $$ = p5_pos_assign(P, G->lineno, P->line, PN_AST(MSG, PN_STR("$_")), p5_unparen(P, e)) }
      | "pos" !utfw - assign e:eqs -
          { $$ = p5_pos_assign(P, G->lineno, P->line, PN_AST(MSG, PN_STR("$_")), p5_unparen(P, e)) }
      | "substr" !utfw - list-start - sv:plainscalar - comma - so:eqs - comma - sl:eqs - list-end - assign e:eqs -
          { $$ = p5_substr_set(P, G->lineno, P->line, sv, so, p5_unparen(P, sl), e) }
      | "substr" !utfw - list-start - sv:plainscalar - comma - so:eqs - list-end - assign e:eqs -
          { $$ = p5_substr_set(P, G->lineno, P->line, sv, so, PN_AST(VALUE, PN_NIL), e) }
      | "--" '$' '#' l:id -  # --$#a: drop the last element
          { $$ = PN_AST(EXPR, PN_PUSH(PN_TUP(PN_AST(MSG, p5_vn(P, "@", l))), PN_AST2(MSG, PN_STR("p5setlast"),
                   PN_AST(LIST, PN_TUP(PN_OP(AST_MINUS, PN_AST(EXPR, PN_PUSH(PN_TUP(PN_AST(MSG, p5_vn(P, "@", l))), PN_AST(MSG, PN_STR("length")))), PN_AST(VALUE, PN_NUM(1)))))))) }
      | "++" '$' '#' l:id -  # ++$#a: grow by one
          { $$ = PN_AST(EXPR, PN_PUSH(PN_TUP(PN_AST(MSG, p5_vn(P, "@", l))), PN_AST2(MSG, PN_STR("p5setlast"),
                   PN_AST(LIST, PN_TUP(PN_OP(AST_PLUS, PN_AST(EXPR, PN_PUSH(PN_TUP(PN_AST(MSG, p5_vn(P, "@", l))), PN_AST(MSG, PN_STR("length")))), PN_AST(VALUE, PN_NUM(1)))))))) }
      | '$' '#' l:id - "--" -  # $#a--
          { $$ = PN_AST(EXPR, PN_PUSH(PN_TUP(PN_AST(MSG, p5_vn(P, "@", l))), PN_AST2(MSG, PN_STR("p5setlast"),
                   PN_AST(LIST, PN_TUP(PN_OP(AST_MINUS, PN_AST(EXPR, PN_PUSH(PN_TUP(PN_AST(MSG, p5_vn(P, "@", l))), PN_AST(MSG, PN_STR("length")))), PN_AST(VALUE, PN_NUM(1)))))))) }
      | '$' '#' l:id - "++" -  # $#a++
          { $$ = PN_AST(EXPR, PN_PUSH(PN_TUP(PN_AST(MSG, p5_vn(P, "@", l))), PN_AST2(MSG, PN_STR("p5setlast"),
                   PN_AST(LIST, PN_TUP(PN_OP(AST_PLUS, PN_AST(EXPR, PN_PUSH(PN_TUP(PN_AST(MSG, p5_vn(P, "@", l))), PN_AST(MSG, PN_STR("length")))), PN_AST(VALUE, PN_NUM(1)))))))) }
      | '$' '#' l:id - assign e:eqs -   # $#a = N: resize @a to N+1 elements
          { $$ = PN_AST(EXPR, PN_PUSH(PN_TUP(PN_AST(MSG, p5_vn(P, "@", l))),
                   PN_AST2(MSG, PN_STR("p5setlast"),
                           PN_AST(LIST, PN_TUP(PN_OP(AST_PLUS, p5_unparen(P, e), PN_AST(VALUE, PN_NUM(1)))))))) }
      | c:elemchain assign e:eqs -  { $$ = p5_elem_chain(P, G->lineno, P->line, c, p5_unparen(P, e)) }
      | MY? lv:lexglobal assign '(' - ')' - assign r:cond-rhs -   # $n = () = LIST: the element count
          { $$ = PN_AST2(ASSIGN, lv, p5_aasign_count(P, G->lineno, P->line, PN_AST(LIST, PN_TUP0()), r)) }
      | l:lexglobal assign e:sets -  { PN asg = PN_AST2(ASSIGN, l, p5_split_scalar(P, G->lineno, P->line, l, p5_scalar_rhs(P, G->lineno, P->line, l, e))); if (!p5_lex_my) p5_top_scalar(asg); $$ = p5_fix_assign(P, G->lineno, P->line, asg) }  # sets: $a = $b = 0
      | l:global assign r:list      { YY_ERROR("** Assignment error") } # @x = () nyi

# right side of 'my (...) = ': a parenthesized list, or a lone array (@_, @a)
listrhs = list
        | '@' '$' i:gid -   # @$ref: elements read as $ref->[i] by p5_list_elem
            { $$ = PN_AST(LIST, PN_TUP(PN_AST(EXPR, PN_TUP(PN_AST(MSG, PN_STRCAT("@$", PN_STR_PTR(i))))))) }
        | '@{' - '$' i:gid - '}' -
            { $$ = PN_AST(LIST, PN_TUP(PN_AST(EXPR, PN_TUP(PN_AST(MSG, PN_STRCAT("@$", PN_STR_PTR(i))))))) }
        | v:listvar  { $$ = PN_AST(LIST, PN_TUP(PN_AST(EXPR, PN_TUP(v)))) }

#TODO most of these stack-like assign-expr cases can probably go away
sets = e:eqs
       ( assign s:sets       { e = PN_AST2(ASSIGN, e, p5_copy_rhs(P, e, s)) }
       | or assign s:sets    { e = PN_AST2(ASSIGN, e, PN_OP(AST_OR, e, s)) }
       | and assign s:sets   { e = PN_AST2(ASSIGN, e, PN_OP(AST_AND, e, s)) }
       | pipe assign s:sets  { e = PN_AST2(ASSIGN, e, p5_bitop(P, G->lineno, P->line, "p5bor", e, s)) }
       | caret assign s:sets { e = PN_AST2(ASSIGN, e, p5_bitop(P, G->lineno, P->line, "p5bxor", e, s)) }
       | amp assign s:sets   { e = PN_AST2(ASSIGN, e, p5_bitop(P, G->lineno, P->line, "p5band", e, s)) }
       | bitl assign s:sets  { e = PN_AST2(ASSIGN, e, PN_OP(AST_BITL, e, s)) }
       | bitr assign s:sets  { e = PN_AST2(ASSIGN, e, PN_OP(AST_BITR, e, s)) }
       | plus assign s:sets  { e = PN_AST2(ASSIGN, e, PN_OP(AST_PLUS, e, s)) }
       | minus assign s:sets { e = PN_AST2(ASSIGN, e, PN_OP(AST_MINUS, e, s)) }
       | times assign s:sets { e = PN_AST2(ASSIGN, e, PN_OP(AST_TIMES, e, s)) }
       | div assign s:sets   { e = PN_AST2(ASSIGN, e, PN_OP(AST_DIV, e, s)) }
       | rem assign s:sets   { e = PN_AST2(ASSIGN, e, PN_OP(AST_REM, e, s)) }
       | pow assign s:sets   { e = PN_AST2(ASSIGN, e, PN_OP(AST_POW, e, s)) }
       | dot assign s:sets   { e = PN_AST2(ASSIGN, e, p5_cat2(P, G->lineno, P->line, e, s)) }
       | "//" assign s:sets  { e = PN_AST2(ASSIGN, e, p5_defor(P, G->lineno, P->line, e, s)) })?
       { p5_top_scalar(e); $$ = p5_fix_assign(P, G->lineno, P->line, e) }

eqterm = c:cmps
      ( numcmp x:cmps       { c = P5N(c); x = P5N(x); c = PN_OP(AST_CMP, PN_OP(AST_PLUS, c, PN_AST(VALUE, PN_ZERO)), PN_OP(AST_PLUS, x, PN_AST(VALUE, PN_ZERO))) } # numify strings
      | strcmpop x:cmps     { c = p5_unparen(P, c); x = p5_unparen(P, x); c = PN_OP(AST_CMP, p5_strval(P, G->lineno, P->line, c), p5_strval(P, G->lineno, P->line, x)) }
      | numeq x:cmps        { c = p5_opmacro(P, G->lineno, P->line, "==", c, x, p5_numeq(P, G->lineno, P->line, P5N(c), P5N(x))) }
      | streq x:cmps        { c = P5N(c); x = P5N(x); c = PN_OP(AST_EQ, p5_strval(P, G->lineno, P->line, c),
                                              p5_strval(P, G->lineno, P->line, x)) }
      | numneq x:cmps       { c = P5N(c); x = P5N(x); c = PN_AST(NOT, p5_numeq(P, G->lineno, P->line, c, x)) }
      | strneq x:cmps       { c = P5N(c); x = P5N(x); c = PN_OP(AST_NEQ, p5_strval(P, G->lineno, P->line, c),
                                               p5_strval(P, G->lineno, P->line, x)) }
      | '=~' - x:rxs -      { c = p5_subst2(P, G->lineno, c, x) }
      | '=~' - x:rxt -      { c = p5_tr(P, G->lineno, c, x) }
      | '=~' - x:regexp     { c = p5_matchval(P, G->lineno, P->line, c, x, 0) }
      | '=~' - x:scalar     { c = p5_matchval(P, G->lineno, P->line, c, x, 0) }
      | '=~' - x:list       { c = p5_matchval(P, G->lineno, P->line, c, p5_unparen(P, PN_AST(EXPR, PN_S(x, 0))), 0) }
      | '=~' - x:str1       { c = p5_matchval(P, G->lineno, P->line, c, PN_AST(VALUE, x), 0) }
      | '=~' - x:str2       { c = p5_matchval(P, G->lineno, P->line, c, x, 0) }
      | '!~' - x:scalar     { c = p5_matchval(P, G->lineno, P->line, c, x, 1) }
      | '!~' - x:str1       { c = p5_matchval(P, G->lineno, P->line, c, PN_AST(VALUE, x), 1) }
      | '!~' - x:str2       { c = p5_matchval(P, G->lineno, P->line, c, x, 1) }
      | '!~' - x:regexp     { c = p5_matchval(P, G->lineno, P->line, c, x, 1) })*
      { $$ = c }

# && binds tighter than || // xor
andeqs = c:eqterm ( and !'=' x:eqterm { c = PN_OP(AST_AND, p5_unparen(P, c), p5_unparen(P, x)) } )*
      { $$ = c }
eqs = c:andeqs
      ( or !'=' x:andeqs       { c = PN_OP(AST_OR, p5_unparen(P, c), p5_unparen(P, x)) }
      | "xor" !utfw - x:andeqs { c = p5_bitop(P, G->lineno, P->line, "p5lxor", c, x) }
      | "//" !'=' - x:andeqs   { c = p5_defor(P, G->lineno, P->line, c, x) })*
      ( '?' - t:eqs - ':' - f:eqs -
        { if (p5_is_wantcond(c)) f = p5_scalar_branch(P, f);
          c = p5_cond(P, G->lineno, P->line, c); c = p5_unparen(P, c); c = PN_AST(EXPR, PN_PUSH(PN_TUP(
                PN_AST3(MSG, PN_if, c,
                             PN_AST(BLOCK, PN_TUP(PN_AST(EXPR, PN_TUPIF(p5_unparen(P, t))))))),
                PN_AST3(MSG, PN_else, PN_NIL,
                             PN_AST(BLOCK, PN_TUP(PN_AST(EXPR, PN_TUPIF(p5_unparen(P, f)))))))) }
      )?
      { $$ = c }


cmps = o:bitors
       ( gte x:bitors        { o = p5_opmacro(P, G->lineno, P->line, ">=", o, x, PN_OP(AST_GTE, P5N(o), P5N(x))) }
       | gt x:bitors         { o = p5_opmacro(P, G->lineno, P->line, ">", o, x, PN_OP(AST_GT, P5N(o), P5N(x))) }
       | lte x:bitors        { o = p5_opmacro(P, G->lineno, P->line, "<=", o, x, PN_OP(AST_LTE, P5N(o), P5N(x))) }
       | lt x:bitors         { o = p5_opmacro(P, G->lineno, P->line, "<", o, x, PN_OP(AST_LT, P5N(o), P5N(x))) }
       | strge x:bitors      { o = p5_strcmp(P, G->lineno, P->line, AST_GTE, o, x) }
       | strgt x:bitors      { o = p5_strcmp(P, G->lineno, P->line, AST_GT, o, x) }
       | strle x:bitors      { o = p5_strcmp(P, G->lineno, P->line, AST_LTE, o, x) }
       | strlt x:bitors      { o = p5_strcmp(P, G->lineno, P->line, AST_LT, o, x) }
       # OBJ isa Name: the class of OBJ or one of its parents is Name (lib/p5/p5class.c)
       | "isa" !utfw - n:qid - { o = PN_AST(EXPR, PN_PUSH(PN_TUP(o), PN_AST2(MSG, PN_STR("p5isa"),
                                   PN_AST(LIST, PN_TUP(PN_AST(VALUE, n)))))) })*
       { $$ = o }

bitors = a:bitand
         ( pipe x:bitand       { a = p5_bitop(P, G->lineno, P->line, "p5bor", a, x) }
         | caret x:bitand      { a = p5_bitop(P, G->lineno, P->line, "p5bxor", a, x) })*
         { $$ = a }

bitand = b:bitshift
         ( amp x:bitshift      { b = p5_bitop(P, G->lineno, P->line, "p5band", b, x) })*
         { $$ = b }

bitshift = s:sum
           ( bitl x:sum          { s = PN_OP(AST_BITL, s, x) }
           | bitr x:sum          { s = PN_OP(AST_BITR, s, x) })*
           { $$ = s }

sum = p:product
      ( plus x:product      { p = p5_opmacro(P, G->lineno, P->line, "+", p, x, PN_OP(AST_PLUS, P5N(p5_cat_operand(P, p)), P5N(x))) }
      | minus x:product     { p = p5_opmacro(P, G->lineno, P->line, "-", p, x, PN_OP(AST_MINUS, P5N(p5_cat_operand(P, p)), P5N(x))) }
      | dot x:product       { p = p5_opmacro(P, G->lineno, P->line, ".", p, x, p5_cat2(P, G->lineno, P->line, P5N(p), P5N(x))) }
      | - op:minfix - x:product { p = p5_mop_call(P, G->lineno, P->line, "infix", op, PN_PUSH(PN_TUP(p), x)) })*
      { $$ = p }
minfix = &{ p5_mop_mark(G) } < ( &. &{ p5_mop_step(G, 2) } . )+ > &{ p5_mop_done(G, 2) } { $$ = PN_STRN(yytext, yyleng) }
mprefix = &{ p5_mop_mark(G) } < ( &. &{ p5_mop_step(G, 0) } . )+ > &{ p5_mop_done(G, 0) } { $$ = PN_STRN(yytext, yyleng) }
mpostfix = &{ p5_mop_mark(G) } < ( &. &{ p5_mop_step(G, 1) } . )+ > &{ p5_mop_done(G, 1) } { $$ = PN_STRN(yytext, yyleng) }

product = p:power
          ( times x:power           { p = p5_opmacro(P, G->lineno, P->line, "*", p, x, PN_OP(AST_TIMES, P5N(p), P5N(x))) }
          | div x:power             { p = p5_opmacro(P, G->lineno, P->line, "/", p, x, PN_OP(AST_DIV, P5N(p), P5N(x))) }
          | rep x:power             { int ll = p5_is_listlit(P, p); p = p5_listlit_array(P, p);
                                      p = PN_AST(EXPR, PN_PUSH(PN_TUP(p),
                                        PN_AST2(MSG, PN_STR(ll ? "p5lrepeat" : "repeat"), PN_AST(LIST, PN_TUP(x))))) }
          | rem x:power             { p = PN_OP(AST_REM, P5N(p), P5N(x)) })*
          { $$ = p }

# ** binds tighter than unary minus and is right associative:
# -3**2 is -(3**2), 2**3**2 is 2**(3**2)
power = op:mprefix - e:power
        { $$ = p5_mop_call(P, G->lineno, P->line, "prefix", op, PN_TUP(e)) }
      | minus e:expr pow x:power
        { $$ = PN_OP(AST_MINUS, PN_AST(VALUE, PN_ZERO), PN_OP(AST_POW, e, x)) }
      | e:expr
        ( - op:mpostfix { e = p5_mop_call(P, G->lineno, P->line, "postfix", op, PN_TUP(e)) } )*
        ( pow x:power { e = PN_OP(AST_POW, e, x) })?
        { $$ = e }

# always a list
expr = o:fhop - "STDERR" !utfw - l:listexprs
        { PN acc = p5_cat(P, G->lineno, P->line, l);
          $$ = PN_AST(EXPR, PN_PUSH(PN_TUPIF(acc), PN_AST(MSG, o))) }
    | < ("say" | "print") > !utfw - &( (semi | '}' | FOR | FOREACH | IF | UNLESS | WHILE | UNTIL | !.))
        { $$ = PN_AST(EXPR, PN_PUSH(PN_TUP(PN_AST(MSG, PN_STR("$_"))),
                                    PN_AST(MSG, PN_STR(yyleng == 3 ? "p5lsay" : "p5lprint")))) }  # bare say/print: $_
    | o:fhop - "STDOUT" !utfw - l:listexprs
        { $$ = PN_AST(EXPR, PN_PUSH(PN_TUPIF(p5_cat(P, G->lineno, P->line, l)),
                                    PN_AST(MSG, PN_STR(PN_STR_LEN(o) == 8 ? "p5lsay" : "p5lprint")))) }
    | o:fhop - h:fhprint - l:listexprs
        { PN acc = p5_cat(P, G->lineno, P->line, l);
          $$ = PN_AST(EXPR, PN_PUSH(PN_TUP(h),
                 PN_AST2(MSG, PN_STR(PN_STR_LEN(o) == 8 ? "p5say" : "p5print"),
                              PN_AST(LIST, PN_TUP(PN_AST(EXPR, PN_TUPIF(acc))))))) }
    | o:fhop - list-start - l:listexprs - list-end &(- (semi | comma | '}' | ')' | !. | IF | UNLESS | FOR | FOREACH | WHILE | UNTIL | or | and))
        { $$ = PN_AST(EXPR, PN_PUSH(PN_TUPIF(p5_cat(P, G->lineno, P->line, l)),
                                    PN_AST(MSG, PN_STR(PN_STR_LEN(o) == 8 ? "p5lsay" : "p5lprint")))) }
    | o:fhop !'(' - l:listexprs &(- (semi | '}' | ')' | !. | IF | UNLESS | FOR | FOREACH | WHILE | UNTIL))
        { $$ = PN_AST(EXPR, PN_PUSH(PN_TUPIF(p5_cat(P, G->lineno, P->line, l)),
                                    PN_AST(MSG, PN_STR(PN_STR_LEN(o) == 8 ? "p5lsay" : "p5lprint")))) }
    | "wantarray" !utfw - ( '(' - ')' )?
        { $$ = PN_AST(EXPR, PN_TUP(PN_AST(MSG, PN_STR("$__wa")))) }
    | "time" !utfw - !'(' !'=>'   # a term without arguments: `time - $start` is a subtraction
        { $$ = PN_AST(EXPR, PN_TUP(PN_AST2(MSG, PN_STR("time"), PN_AST(LIST, PN_TUP0())))) }
    | "split" !utfw - &(semi | '}' | ')' | !.)
        { $$ = p5_call_padded(P, G->lineno, P->line, "split",
                 PN_PUSH(PN_TUP(potion_source(P, AST_VALUE, PN_STRN("\001", 1), PN_NIL, PN_NIL, G->lineno, P->line)),
                         PN_AST(MSG, PN_STR("$_")))) }
    | "split" !utfw - list-start - c:splitpat - comma - l:listexprs - list-end -
        { $$ = p5_call_padded(P, G->lineno, P->line, "split", p5_prepend(P, c, l)) }
    | "split" !utfw - list-start - c:splitpat - list-end -
        { $$ = p5_call_padded(P, G->lineno, P->line, "split", PN_PUSH(PN_TUP(c), PN_AST(MSG, PN_STR("$_")))) }
    | "split" !utfw - c:splitpat - &(semi | '}' | ')' | !.)
        { $$ = p5_call_padded(P, G->lineno, P->line, "split", PN_PUSH(PN_TUP(c), PN_AST(MSG, PN_STR("$_")))) }
    | "split" !utfw - c:splitpat - comma - l:listexprs
        { $$ = p5_call_padded(P, G->lineno, P->line, "split", p5_prepend(P, c, l)) }
    | "printf" !utfw - "STDERR" !utfw - l:listexprs  { $$ = p5_printf_fh(P, G->lineno, P->line, PN_NIL, "eprint", l) }
    | "printf" !utfw - "STDOUT" !utfw - l:listexprs  { $$ = p5_printf_fh(P, G->lineno, P->line, PN_NIL, "p5lprint", l) }
    | "printf" !utfw - h:fhprint - l:listexprs  { $$ = p5_printf_fh(P, G->lineno, P->line, h, "p5print", l) }
    | "printf" !utfw - list-start - l:listexprs - list-end -  { $$ = p5_printf(P, G->lineno, P->line, l) }
    | "printf" !utfw - l:listexprs                           { $$ = p5_printf(P, G->lineno, P->line, l) }
    | "open" !utfw - list-start - t:fhtarget - comma - l:listexprs - list-end -
        { $$ = p5_open(P, G->lineno, P->line, t, l) }
    | "open" !utfw - t:fhtarget - comma - l:listexprs
        { $$ = p5_open(P, G->lineno, P->line, t, l) }
    | "opendir" !utfw - list-start - t:fhtarget - comma - e:eqs - list-end -
        { $$ = potion_source(P, AST_ASSIGN, t, potion_source(P, AST_EXPR, PN_TUP(potion_source(P, AST_MSG, PN_STR("opendir"),
            potion_source(P, AST_LIST, PN_TUP(p5_unparen(P, e)), PN_NIL, PN_NIL, G->lineno, P->line), PN_NIL, G->lineno, P->line)), PN_NIL, PN_NIL, G->lineno, P->line), PN_NIL, G->lineno, P->line) }
    | "opendir" !utfw - t:fhtarget - comma - e:eqs
        { $$ = potion_source(P, AST_ASSIGN, t, potion_source(P, AST_EXPR, PN_TUP(potion_source(P, AST_MSG, PN_STR("opendir"),
            potion_source(P, AST_LIST, PN_TUP(p5_unparen(P, e)), PN_NIL, PN_NIL, G->lineno, P->line), PN_NIL, G->lineno, P->line)), PN_NIL, PN_NIL, G->lineno, P->line), PN_NIL, G->lineno, P->line) }
    | "fileno" !utfw - list-start - "STDIN" !utfw - list-end -  { $$ = PN_AST(VALUE, PN_NUM(0)) }
    | "fileno" !utfw - list-start - "STDOUT" !utfw - list-end -  { $$ = PN_AST(VALUE, PN_NUM(1)) }
    | "fileno" !utfw - list-start - "STDERR" !utfw - list-end -  { $$ = PN_AST(VALUE, PN_NUM(2)) }
    | "fileno" !utfw - "STDIN" !utfw  { $$ = PN_AST(VALUE, PN_NUM(0)) }
    | "fileno" !utfw - "STDOUT" !utfw  { $$ = PN_AST(VALUE, PN_NUM(1)) }
    | "fileno" !utfw - "STDERR" !utfw  { $$ = PN_AST(VALUE, PN_NUM(2)) }
    | o:fhfn !utfw - list-start - h:fhval - list-end -
        { $$ = PN_AST(EXPR, PN_TUP(PN_AST2(MSG, o, PN_AST(LIST, PN_TUP(h))))) }
    | o:fhfn !utfw - h:fhval !(- comma)
        { $$ = PN_AST(EXPR, PN_TUP(PN_AST2(MSG, o, PN_AST(LIST, PN_TUP(h))))) }
    | "flock" !utfw - list-start - h:fhval - comma - n:eqs - list-end -
        { $$ = PN_AST(EXPR, PN_TUP(PN_AST2(MSG, PN_STR("flock"), PN_AST(LIST, PN_PUSH(PN_TUP(h), p5_unparen(P, n)))))) }
    | "flock" !utfw - h:fhval - comma - n:eqs
        { $$ = PN_AST(EXPR, PN_TUP(PN_AST2(MSG, PN_STR("flock"), PN_AST(LIST, PN_PUSH(PN_TUP(h), p5_unparen(P, n)))))) }
    | "truncate" !utfw - list-start - h:fhval - comma - n:eqs - list-end -
        { $$ = PN_AST(EXPR, PN_TUP(PN_AST2(MSG, PN_STR("truncate"), PN_AST(LIST, PN_PUSH(PN_TUP(h), p5_unparen(P, n)))))) }
    | "truncate" !utfw - h:fhval - comma - n:eqs
        { $$ = PN_AST(EXPR, PN_TUP(PN_AST2(MSG, PN_STR("truncate"), PN_AST(LIST, PN_PUSH(PN_TUP(h), p5_unparen(P, n)))))) }
    | o:dirop !utfw - list-start - h:fhval - list-end -
        { $$ = PN_AST(EXPR, PN_PUSH(PN_TUP(h), PN_AST(MSG, o))) }
    | o:dirop !utfw - h:fhval !(- comma)
        { $$ = PN_AST(EXPR, PN_PUSH(PN_TUP(h), PN_AST(MSG, o))) }
    | "close" !utfw - list-start - h:fhval - list-end -
        { $$ = PN_AST(EXPR, PN_PUSH(PN_TUP(h), PN_AST(MSG, PN_STR("p5close")))) }
    | "close" !utfw - h:fhval !(- comma)
        { $$ = PN_AST(EXPR, PN_PUSH(PN_TUP(h), PN_AST(MSG, PN_STR("p5close")))) }
    | ("read" | "sysread") !utfw - list-start - h:fhval - comma - v:scalar - comma - n:eqs - comma - o:eqs - list-end -
        { $$ = p5_read_ast(P, G->lineno, P->line, h, v, n, o) }
    | ("read" | "sysread") !utfw - list-start - h:fhval - comma - v:scalar - comma - n:eqs - list-end -
        { $$ = p5_read_ast(P, G->lineno, P->line, h, v, n, PN_NIL) }
    | ("seek" | "sysseek") !utfw - list-start - h:fhval - comma - p:eqs - comma - w:eqs - list-end -
        { $$ = PN_AST(EXPR, PN_PUSH(PN_TUP(h), PN_AST2(MSG, PN_STR("p5seek"), PN_AST(LIST, PN_PUSH(PN_TUP(p5_unparen(P, p)), p5_unparen(P, w)))))) }
    | "tell" !utfw - list-start - h:fhval - list-end -
        { $$ = PN_AST(EXPR, PN_PUSH(PN_TUP(h), PN_AST(MSG, PN_STR("p5tell")))) }
    | "eof" !utfw - list-start - h:fhval - list-end -
        { $$ = PN_AST(EXPR, PN_PUSH(PN_TUP(h), PN_AST(MSG, PN_STR("p5eof")))) }
    | "eof" !utfw - h:fhname           # eof FH, ...: a bareword handle takes no list
        { $$ = PN_AST(EXPR, PN_PUSH(PN_TUP(h), PN_AST(MSG, PN_STR("p5eof")))) }
    | "eof" !utfw - h:fhval !(- comma)
        { $$ = PN_AST(EXPR, PN_PUSH(PN_TUP(h), PN_AST(MSG, PN_STR("p5eof")))) }
    | "eof" !utfw - ('(' - ')' -)? !'=>'   # no handle: the last one read
        { $$ = PN_AST(EXPR, PN_TUP(PN_AST2(MSG, PN_STR("p5eofl"), PN_AST(LIST, PN_TUP0())))) }
    | r:readline            { $$ = r }
    | "scalar" !utfw - list-start - MY? l:listvar assign e2:eqs - list-end -   # scalar(my @x = LIST): the count
        { $$ = PN_AST(EXPR, PN_PUSH(PN_TUP(PN_AST2(ASSIGN, l, e2)), PN_AST(MSG, PN_STR("length")))) }
    | "scalar" !utfw - "reverse" !utfw - list-start - x:eqs - comma - l:listexprs - list-end -      # scalar reverse(A, B): the concatenation reversed
        { PN cat = p5_cat(P, G->lineno, P->line, p5_prepend(P, x, l));
          $$ = PN_AST(EXPR, PN_PUSH(PN_TUPIF(cat), PN_AST(MSG, PN_STR("reverse")))) }
    | "scalar" !utfw - "reverse" !utfw - list-start - list-end -      # scalar reverse(): $_ reversed
        { $$ = PN_AST(EXPR, PN_PUSH(PN_TUP(PN_AST(MSG, PN_STR("$_"))), PN_AST(MSG, PN_STR("reverse")))) }
    | "scalar" !utfw - "reverse" !utfw - list-start - x:eqs - list-end -      # scalar reverse(STR)
        { $$ = PN_AST(EXPR, PN_PUSH(PN_TUPIF(p5_unparen(P, x)), PN_AST(MSG, PN_STR("reverse")))) }
    | "scalar" !utfw - list-start - "reverse" !utfw - list-start - x:eqs - comma - l:listexprs - list-end - list-end -
        { PN cat = p5_cat(P, G->lineno, P->line, p5_prepend(P, x, l));
          $$ = PN_AST(EXPR, PN_PUSH(PN_TUPIF(cat), PN_AST(MSG, PN_STR("reverse")))) }
    | "scalar" !utfw - list-start - "reverse" !utfw - list-start - list-end - list-end -
        { $$ = PN_AST(EXPR, PN_PUSH(PN_TUP(PN_AST(MSG, PN_STR("$_"))), PN_AST(MSG, PN_STR("reverse")))) }
    | "scalar" !utfw - list-start - "reverse" !utfw - x:eqs - list-end -      # scalar(reverse STR): string reverse
        { $$ = PN_AST(EXPR, PN_PUSH(PN_TUPIF(p5_unparen(P, x)), PN_AST(MSG, PN_STR("reverse")))) }
    | "scalar" !utfw - list-start - e:eqs - list-end -
        { $$ = p5_scalar_of(P, G->lineno, P->line, e) }
    | "scalar" !utfw - e:eqs
        { $$ = p5_scalar_of(P, G->lineno, P->line, e) }
    | "map" !utfw - b:lbblock l:listexprs
        { $$ = p5_listop(P, G->lineno, P->line, "p5map", b, l) }
    | "grep" !utfw - b:lbblock l:listexprs
        { $$ = p5_listop(P, G->lineno, P->line, "p5grep", b, l) }
    | "map" !utfw - list-start - b:lbblock - comma? - l:listexprs - list-end -
        { $$ = p5_listop(P, G->lineno, P->line, "p5map", b, l) }
    | "grep" !utfw - list-start - b:lbblock - comma? - l:listexprs - list-end -
        { $$ = p5_listop(P, G->lineno, P->line, "p5grep", b, l) }
    # map EXPR, LIST / grep EXPR, LIST: the expression is the block
    | "map" !utfw - list-start - e:eqs - comma - l:listexprs - list-end -
        { $$ = p5_listop(P, G->lineno, P->line, "p5map", PN_AST(BLOCK, PN_TUP(e)), l) }
    | "map" !utfw - e:eqs - comma - l:listexprs
        { $$ = p5_listop(P, G->lineno, P->line, "p5map", PN_AST(BLOCK, PN_TUP(e)), l) }
    | "grep" !utfw - list-start - e:eqs - comma - l:listexprs - list-end -
        { $$ = p5_listop(P, G->lineno, P->line, "p5grep", PN_AST(BLOCK, PN_TUP(e)), l) }
    | "grep" !utfw - e:eqs - comma - l:listexprs
        { $$ = p5_listop(P, G->lineno, P->line, "p5grep", PN_AST(BLOCK, PN_TUP(e)), l) }
    | "reverse" !utfw - !'(' r:range !(- comma)    # reverse 1..3: the range is the list
        { $$ = PN_AST(EXPR, PN_PUSH(PN_TUP(r), PN_AST(MSG, PN_STR("reverse")))) }
    | "reverse" !utfw - list-start - r1:rangeset - comma - l:listexprs - list-end -
        { PN items = potion_source(P, AST_LIST, p5_prepend(P, r1, l), PN_NIL, PN_NIL, G->lineno, P->line);
          PN f = p5_flatten_multi(P, G->lineno, P->line, items);
          $$ = PN_AST(EXPR, PN_PUSH(PN_TUP(f), PN_AST(MSG, PN_STR("reverse")))) }
    | "reverse" !utfw - !'(' r1:rangeset - comma - l:listexprs
        { PN items = potion_source(P, AST_LIST, p5_prepend(P, r1, l), PN_NIL, PN_NIL, G->lineno, P->line);
          PN f = p5_flatten_multi(P, G->lineno, P->line, items);
          $$ = PN_AST(EXPR, PN_PUSH(PN_TUP(f), PN_AST(MSG, PN_STR("reverse")))) }
    | "join" !utfw - list-start - s1:eqs - comma - l:listexprs - list-end -
        { $$ = p5_join_node(P, G->lineno, P->line, s1, l) }
    | "join" !utfw - s1:eqs - comma - l:listexprs
        { $$ = p5_join_node(P, G->lineno, P->line, s1, l) }
    | "sort" !utfw - b:lbblock l:listexprs
        { $$ = p5_listop(P, G->lineno, P->line, "p5sort", b, l) }
    # sort SUBNAME LIST / sort $subref LIST: the comparator reads $a and $b
    | "sort" !utfw - n:sortsub - l:listexprs
        { $$ = p5_listop(P, G->lineno, P->line, "p5sort", PN_AST(BLOCK, PN_TUP(PN_AST(EXPR, PN_TUP(PN_AST2(MSG, n, PN_AST(LIST, PN_NIL)))))), l) }
    | "sort" !utfw - '$' v:gid - &('@' | '(' | '$' | "keys" | "values" | "map" | "grep" | "reverse") l:listexprs
        { $$ = p5_listop(P, G->lineno, P->line, "p5sort",
                 PN_AST(BLOCK, PN_TUP(p5_callv_node(P, G->lineno, P->line, PN_AST(MSG, p5_vn(P, "$", v)), PN_AST(LIST, PN_NIL)))), l) }
    | "sort" !utfw - list-start - l:listexprs - list-end -
        { $$ = p5_listop(P, G->lineno, P->line, "p5sort", PN_NIL, l) }
    | "sort" !utfw - l:listexprs
        { $$ = p5_listop(P, G->lineno, P->line, "p5sort", PN_NIL, l) }
    | "do" !utfw - b:block   { $$ = PN_AST(EXPR, PN_TUP(PN_AST3(MSG, PN_STR("p5do"), PN_NIL, b))) }
    | "pos" !utfw - list-start - s:plainscalar - list-end -   { $$ = p5_pos(P, G->lineno, P->line, s) }
    | "pos" !utfw - s:plainscalar                            { $$ = p5_pos(P, G->lineno, P->line, s) }
    | "pos" !utfw - list-start - list-end -                  { $$ = p5_pos(P, G->lineno, P->line, PN_AST(MSG, PN_STR("$_"))) }
    | "pos" !utfw - &(',' | ')' | ';' | '}' | '=' !'>' | '?' | ':' | !.)  { $$ = p5_pos(P, G->lineno, P->line, PN_AST(MSG, PN_STR("$_"))) }   # bare pos: $_
    | "chop" !utfw - '(' - ')' -   { $$ = p5_chop(P, G->lineno, P->line, PN_AST(MSG, PN_STR("$_"))) }
    | "chop" !utfw - &(semi | '}' | FOR | FOREACH | IF | UNLESS | WHILE | UNTIL | !.)
        { $$ = p5_chop(P, G->lineno, P->line, PN_AST(MSG, PN_STR("$_"))) }
    | "chop" !utfw - '(' - l:listvar - ')' -   { $$ = p5_chop(P, G->lineno, P->line, l) }
    | "chop" !utfw - '(' - s:plainscalar ')' -   { $$ = p5_chop(P, G->lineno, P->line, s) }
    | "chop" !utfw - s:plainscalar               { $$ = p5_chop(P, G->lineno, P->line, s) }
    | "chomp" !utfw - '(' - ')' -   { $$ = p5_chomp(P, G->lineno, P->line, PN_AST(MSG, PN_STR("$_"))) }
    | "chomp" !utfw - &(semi | '}' | FOR | FOREACH | IF | UNLESS | WHILE | UNTIL | !.)
        { $$ = p5_chomp(P, G->lineno, P->line, PN_AST(MSG, PN_STR("$_"))) }
    | "chomp" !utfw - '(' - MY? s:plainscalar - assign e:eqs - ')' -   { $$ = p5_chomp_assign(P, G->lineno, P->line, s, p5_unparen(P, e)) }
    | "chomp" !utfw - '(' - l:listvar - ')' -   { $$ = p5_chomp(P, G->lineno, P->line, l) }
    | "chomp" !utfw - '(' - s:plainscalar ')' -   { $$ = p5_chomp(P, G->lineno, P->line, s) }
    | "chomp" !utfw - s:plainscalar               { $$ = p5_chomp(P, G->lineno, P->line, s) }
    | e:blockeval              { $$ = e }
    | e:stringeval             { $$ = e }
    | c:elemchain !(- '->' - '(') !(- (pplus | mminus))    { $$ = p5_elem_chain(P, G->lineno, P->line, c, PN_NIL) }
    | c:p5exists       { $$ = PN_AST(EXPR, c) }
    | c:p5push         { $$ = c }
    | d:p5delslice     { $$ = PN_AST(EXPR, d) }
    | c:p5splice       { $$ = c }
    | "substr" !utfw - list-start - sv:plainscalar - comma - so:eqs - comma - sl:eqs - comma - sr:eqs - list-end -
        { $$ = p5_substr_set(P, G->lineno, P->line, sv, so, p5_unparen(P, sl), sr) }
    | c:p5delete       { $$ = PN_AST(EXPR, c) }
    | c:p5tie          { $$ = c }
    | c:p5coderef       { $$ = PN_AST(EXPR, c) }
    | c:loopctl         { $$ = PN_AST(EXPR, PN_TUP(c)) }
    | c:method  	        { $$ = PN_AST(EXPR, c) }
    | m:special l:list b:block  { PN_SRC(m)->a[1] = PN_SRC(l); PN_TOUCH(m);
            PN_SRC(m)->a[2] = PN_SRC(b); PN_TOUCH(m);
            $$ = PN_AST(EXPR, PN_TUP(m)) }
    | e:q                   { $$ = PN_AST(EXPR, PN_TUPIF(e)) }
    | e:qrexp               { $$ = PN_AST(EXPR, PN_TUPIF(e)) }
    | x:rxs -               { $$ = p5_subst2(P, G->lineno, PN_AST(EXPR, PN_TUP(PN_AST(MSG, PN_STR("$_")))), x) }
    | x:rxt -               { $$ = p5_tr(P, G->lineno, PN_AST(EXPR, PN_TUP(PN_AST(MSG, PN_STR("$_")))), x) }
    # a bare /pattern/ matches $_
    | x:regexp              { $$ = p5_matchval(P, G->lineno, P->line,
                                  PN_AST(EXPR, PN_TUP(PN_AST(MSG, PN_STR("$_")))), x, 0) }
    | e:qq                  { $$ = PN_AST(EXPR, PN_TUPIF(e)) }
    | e:qx                  { $$ = PN_AST(EXPR, PN_TUPIF(e)) }
    | e:bq                  { $$ = PN_AST(EXPR, PN_TUPIF(e)) }
    | e:qw '[' - i:listexprs - ']' -      # qw(a b c)[1,2]: a slice of the words
        { $$ = PN_AST(EXPR, PN_PUSH(PN_TUP(PN_AST(EXPR, PN_TUPIF(e))), p5_sliceof(P, G->lineno, P->line, i))) }
    | e:qw                  { $$ = PN_AST(EXPR, PN_TUPIF(e)) }
    # undef $x: assign undef to the scalar (undef alone is the value)
    | "undef" !utfw - x:scalar
        { $$ = PN_AST(EXPR, PN_TUP(p5_fix_assign(P, G->lineno, P->line, PN_AST2(ASSIGN, x, PN_AST(VALUE, PN_NIL))))) }
    | "undef" !utfw - l:listvar &(- (semi | '}' | ')' | !.))
        { $$ = PN_AST(EXPR, PN_TUP(PN_AST2(ASSIGN, l, PN_AST(LIST, PN_NIL)))) }
    | "undef" !utfw - l:hashvar &(- (semi | '}' | ')' | !.))
        { $$ = PN_AST(EXPR, PN_TUP(PN_AST2(ASSIGN, l, PN_AST(EXPR, PN_PUSH(PN_TUP(PN_AST(LIST, PN_NIL)), PN_AST(MSG, PN_STR("table"))))))) }
    # a bare undef is the value (not a call that would take `// 2` as its argument)
    | "undef" !utfw - !'$' !'@' !'%' !'(' !'\\'
        { $$ = PN_AST(EXPR, PN_TUP(PN_AST(VALUE, PN_NIL))) }
    # reset / reset 'X': clears the m?pat? one-time match states (all, or those
    # whose pattern begins with the argument)
    | "reset" !utfw - e:bitshift &( - (semi | '}' | ')' | !.))
        { $$ = PN_AST(EXPR, PN_TUP(PN_AST2(MSG, PN_STR("p5reset"),
              PN_AST(LIST, PN_TUP(e))))) }
    | "reset" !utfw - &( - (semi | '}' | ')' | !.))
        { $$ = PN_AST(EXPR, PN_TUP(PN_AST2(MSG, PN_STR("p5reset"),
              PN_AST(LIST, PN_TUP(PN_AST(VALUE, PN_NIL)))))) }
    # -e FILE -f -d -r -w -x -s -z ...: file tests, named unary operators (FILE: a string, a variable, (expr) or _)
    | '-' o:ftletter !utfw - '_' !utfw -
        { $$ = PN_AST(EXPR, PN_TUP(PN_AST2(MSG, PN_STR("p5ftest"), PN_AST(LIST, PN_PUSH(PN_TUP(p5_str_value(P, o)), p5_str_value(P, PN_STR("_"))))))) }
    | '-' o:ftletter !utfw - &('-' ftletter !utfw) e:bitshift      # -f -e $file: stacked, -e $file && -f _
        { $$ = PN_OP(AST_AND, e, PN_AST(EXPR, PN_TUP(PN_AST2(MSG, PN_STR("p5ftest"), PN_AST(LIST, PN_PUSH(PN_TUP(p5_str_value(P, o)), p5_str_value(P, PN_STR("_")))))))) }
    | '-' o:ftletter !utfw - &(["'$(]) e:bitshift
        { $$ = PN_AST(EXPR, PN_TUP(PN_AST2(MSG, PN_STR("p5ftest"), PN_AST(LIST, PN_PUSH(PN_TUP(p5_str_value(P, o)), PN_AST(EXPR, PN_TUPIF(e))))))) }
    # defined EXPR / defined(EXPR): a named unary operator, true unless undef
    | "defined" !utfw - list-start '&' dn:qid list-end -
        { $$ = PN_AST(EXPR, PN_TUP(p5_sub_exists(P, G->lineno, P->line, p5_qname(P, dn)))) }
    | "defined" !utfw - '&' dn:qid -
        { $$ = PN_AST(EXPR, PN_TUP(p5_sub_exists(P, G->lineno, P->line, p5_qname(P, dn)))) }
    | "defined" !utfw - list-start e:eqs list-end -
        { $$ = PN_OP(AST_NEQ, p5_is_ternary(e) ? p5_cond(P, G->lineno, P->line, e) : e, PN_AST(VALUE, PN_NIL)) }
    | "defined" !utfw - e:bitshift
        { $$ = PN_OP(AST_NEQ, e, PN_AST(VALUE, PN_NIL)) }
    | '*' n:qid '{' - sl:id - '}'      # *name{CODE}, *name{ARRAY}, ...: a slot of the glob
        { $$ = PN_AST(EXPR, PN_TUP(PN_AST2(MSG, PN_STR("p5globslot"), PN_AST(LIST, PN_PUSH(PN_PUSH(PN_TUP(p5_str_value(P, p5_qname(P, n))), p5_str_value(P, PN_STR(""))), p5_str_value(P, sl)))))) }
    # (LIST)[INDEXES]: a list slice; one plain index is the element itself
    | &(balparen - '[') l:list - '[' - i:listexprs - ']' -
        { PN f = p5_flatten_multi(P, G->lineno, P->line, l), rhs = f != l ? f : p5_flatten1(P, l);
          if (PN_IS_TUPLE(PN_S(l, 0)) && PN_TUPLE_LEN(PN_S(l, 0)) == 1 && p5_is_call_item(PN_TUPLE_AT(PN_S(l, 0), 0)))
            rhs = PN_AST(EXPR, PN_TUP(PN_AST2(MSG, PN_STR("p5aslist"), PN_AST(LIST, PN_TUP(PN_TUPLE_AT(PN_S(l, 0), 0))))));
          if (PN_TUPLE_LEN(i) == 1 && !p5_arrayish(P, PN_TUPLE_AT(i, 0)))
            $$ = PN_AST(EXPR, PN_PUSH(PN_TUP(rhs), PN_AST2(MSG, PN_STR("at"), PN_AST(LIST, PN_TUP(p5_index(P, PN_TUPLE_AT(i, 0)))))));
          else $$ = PN_AST(EXPR, PN_PUSH(PN_TUP(rhs), p5_sliceof(P, G->lineno, P->line, i))) }
    | c:calllist - k:elemstep1 { $$ = k = PN_PUSH(PN_TUP(PN_AST(EXPR, c)), k) }
        ( k2:elemstep { $$ = k = PN_PUSH(k, k2) } )*
        { $$ = p5_elem_chain(P, G->lineno, P->line, k, PN_NIL) }
    # f(args)->m(..)->n: methods and calls on the value of a function call (keys(%h)->length)
    | c:calllist - st:methstep { $$ = c = p5_meth_step(P, PN_TUPIF(c), st) }
        ( - st2:methstep { $$ = c = p5_meth_step(P, c, st2) } )*
        { $$ = PN_AST(EXPR, c) }
    | c:calllist		{ $$ = PN_AST(EXPR, c) }
    # named unary operator without parens binds tighter than comparison:
    # 'ord "A" == 65' is '(ord "A") == 65'
    # a named unary operator takes exactly one argument: 'int "4x", "\n"' ends at the comma
    | u:p5nunary e:bitshift
        { $$ = PN_AST(EXPR, PN_PUSH(PN_TUPIF(e), u)) }
    | &(("keys" | "values" | "each") !utfw - [%@$]) u:p5unary e:bitshift    # keys %h, "\n": the comma ends the operand
        { $$ = PN_AST(EXPR, PN_PUSH(PN_TUPIF(e), u)) }
    | u:p5unary e:bitshift !(- (comma|fatcomma))
        { $$ = PN_AST(EXPR, PN_PUSH(PN_TUPIF(e), u)) }
    # bare 'shift' / 'pop' operate on @_, bare each/keys/values die, the other named unary operators on $_
    | u:p5unary
        { $$ = p5_bare_unary(P, G->lineno, P->line, u) }
    | u:p5nunary
        { $$ = PN_AST(EXPR, PN_PUSH(PN_TUP(PN_AST(MSG, PN_STR("$_"))), u)) }
    # name BLOCK LIST for a sub with a (&...) prototype
    | &( < [A-Za-z_] [A-Za-z0-9_]* > &{ p5_blocksub(yytext, (long)strlen(yytext)) } ) c:call - b:block - l:listexprs
        { $$ = p5_block_call(P, G->lineno, P->line, c, b, l) }
    | &( < [A-Za-z_] [A-Za-z0-9_]* > &{ p5_blocksub(yytext, (long)strlen(yytext)) } ) c:call - b:block
        { $$ = p5_block_call(P, G->lineno, P->line, c, b, PN_NIL) }
    | c:call l:listexprs 	{ PN fm = PN_TUPLE_AT(c, 0);
            int mc = PN_S(fm, 1) == PN_NIL && p5_macro_call(P, G->lineno, P->line, fm, p5_unparen_args(P, l)); /* NAME args: a macro call without parens */
            PN sc = mc ? PN_NIL : p5_spread_call(P, G->lineno, P->line, fm, p5_unparen_args(P, l));
            if (mc) $$ = PN_AST(EXPR, c);
            else if (sc != PN_NIL) $$ = PN_AST(EXPR, sc);
            else { PN_SRC(fm)->a[1] = PN_SRC(PN_AST(LIST, p5_pad_args(P, fm, l))); PN_TOUCH(fm);
              $$ = PN_AST(EXPR, c); } }
            ( arrow l2:list - { $$ = p5_callv_node(P, G->lineno, P->line, $$, l2) } )*   # f(1)->(2)
    | e:opexpr			{ $$ = e }
    | e:pkgword			{ $$ = PN_AST(EXPR, PN_TUP(e)) }
    | c:call			{ $$ = p5_bare_call(P, G->lineno, P->line, c) }
    | e:eatom

eatom = e:atom                  { $$ = PN_AST(EXPR, PN_TUPIF(e)) }

opexpr = '\\' - b:plainscalar !'[' !'{' !arrow  { $$ = p5_scalar_ref(P, G->lineno, P->line, b) }
    | '\\' '&' i:qid -       { $$ = PN_AST(EXPR, PN_TUP(p5_qualify_call(P, PN_AST(MSG, i)))) }   # \&f: the sub itself (of the package)
    | '\\' - e:expr		{ $$ = p5_is_arrvar(e) ? PN_AST(EXPR, PN_PUSH(PN_TUP(e), PN_AST(MSG, PN_STR("p5refid")))) : PN_AST(EXPR, PN_TUP(e)) }  # \@a, \%h, \&f: the object itself (wrapped: not a count); \$x copies
    | "not" !utfw - e:eqs	{ $$ = PN_AST(NOT, p5_cond(P, G->lineno, P->line, e)) }   # low precedence: not $a eq $b
    | not e:expr		{ $$ = PN_AST(NOT, p5_cond(P, G->lineno, P->line, e)) }
    | bitnot e:expr		{ $$ = PN_AST(WAVY, e) }
    | '+' !'+' - e:expr		{ $$ = e }  # unary plus
    | !"--" !('-' ([1-9.] | '0' ![0-9xXbB_])) minus e:expr		{ $$ = PN_OP(AST_MINUS, PN_AST(VALUE, PN_ZERO), e) }  # -7 is one literal token
    | k:incop e:elemlike     { $$ = p5_elem_incdec(P, G->lineno, P->line, e, (int)PN_INT(k), 1) }
    | e:elemlike k:incop     { $$ = p5_elem_incdec(P, G->lineno, P->line, e, (int)PN_INT(k), 0) }
    | mminus e:mvalue		{ $$ = PN_AST2(ASSIGN, e, PN_OP(AST_MINUS, e, PN_AST(VALUE, PN_NUM(1)))) } 
    | pplus e:mvalue		{ $$ = PN_AST2(ASSIGN, e, PN_OP(AST_PLUS, e, PN_AST(VALUE, PN_NUM(1)))) } 
    | '${' - ie:eqs - '}' pplus !'[' !'{' { PN dd = p5_deref(P, G->lineno, P->line, ie); $$ = PN_OP(AST_MINUS, p5_fix_assign(P, G->lineno, P->line, PN_AST2(ASSIGN, dd, PN_OP(AST_PLUS, dd, PN_AST(VALUE, PN_NUM(1))))), PN_AST(VALUE, PN_NUM(1))) }
    | '${' - ie:eqs - '}' mminus !'[' !'{' { PN dd = p5_deref(P, G->lineno, P->line, ie); $$ = PN_OP(AST_PLUS, p5_fix_assign(P, G->lineno, P->line, PN_AST2(ASSIGN, dd, PN_OP(AST_MINUS, dd, PN_AST(VALUE, PN_NUM(1))))), PN_AST(VALUE, PN_NUM(1))) }
    | d:dscalar pplus !'[' { $$ = PN_OP(AST_MINUS, p5_fix_assign(P, G->lineno, P->line, PN_AST2(ASSIGN, d, PN_OP(AST_PLUS, d, PN_AST(VALUE, PN_NUM(1))))), PN_AST(VALUE, PN_NUM(1))) }
    | d:dscalar mminus !'[' { $$ = PN_OP(AST_PLUS, p5_fix_assign(P, G->lineno, P->line, PN_AST2(ASSIGN, d, PN_OP(AST_MINUS, d, PN_AST(VALUE, PN_NUM(1))))), PN_AST(VALUE, PN_NUM(1))) }
    | e:mvalue (pplus		{ $$ = p5_sess_var(e) ? PN_OP(AST_MINUS, PN_AST2(ASSIGN, e, PN_OP(AST_PLUS, e, PN_AST(VALUE, PN_NUM(1)))), PN_AST(VALUE, PN_NUM(1))) : PN_OP(AST_INC, e, PN_NUM(1)) }
             | mminus		{ $$ = p5_sess_var(e) ? PN_OP(AST_PLUS, PN_AST2(ASSIGN, e, PN_OP(AST_MINUS, e, PN_AST(VALUE, PN_NUM(1)))), PN_AST(VALUE, PN_NUM(1))) : PN_OP(AST_INC, e, PN_NUM(-1)) }) {}

atom = e:quasi | e:unquote | e:anonhash | e:value | e:list | e:anonsub | e:qw

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
calllist = u:p5nunary - list-start - list-end -   # length(): the function of $_
           { $$ = PN_PUSH(PN_TUP(PN_AST(MSG, PN_STR("$_"))), u) }
         | u:p5unary - list-start e:callitem - list-end -
           { $$ = PN_PUSH(PN_TUPIF(e), u) }
         | m:name - list-start - list-end
           { p5_qualify_call(P, m);
             if (!p5_macro_call(P, G->lineno, P->line, m, PN_NIL)) { PN_SRC(m)->a[1] = PN_SRC(PN_AST(LIST, PN_NIL)); PN_TOUCH(m); }
             $$ = PN_TUP(m) }
         | m:name - l:list -
           { PN sc = (p5_qualify_call(P, m), PN_NIL);
             int mc = p5_macro_call(P, G->lineno, P->line, m, p5_unparen_args(P, PN_S(l, 0)));
             if (!mc) sc = p5_spread_call(P, G->lineno, P->line, m, p5_unparen_args(P, PN_S(l, 0)));
             if (mc) $$ = PN_TUP(m);
             else if (sc != PN_NIL) $$ = sc;
             else { PN_SRC(m)->a[1] = PN_SRC(PN_AST(LIST, p5_pad_args(P, m, PN_S(l, 0)))); PN_TOUCH(m); $$ = PN_TUP(m); } }
         | m:name - list-start l:callexprs list-end -
           { PN sc = (p5_qualify_call(P, m), PN_NIL);
             int mc = p5_macro_call(P, G->lineno, P->line, m, p5_unparen_args(P, l));
             if (!mc) sc = p5_spread_call(P, G->lineno, P->line, m, p5_unparen_args(P, l));
             if (mc) $$ = PN_TUP(m);
             else if (sc != PN_NIL) $$ = sc;
             else { PN_SRC(m)->a[1] = PN_SRC(PN_AST(LIST, p5_pad_args(P, m, l))); PN_TOUCH(m); $$ = PN_TUP(m); } }
# perl's two evals: entertry (eval BLOCK, caught die) and entereval (eval EXPR, parsed at run time).
# EXPR is a named unary operand: `eval "a", "b"` is (eval "a"), "b".
blockeval  = "eval" !utfw - b:block   { $$ = PN_AST(EXPR, PN_TUP(PN_AST3(MSG, PN_STR("p5eval"), PN_NIL, b))) }
stringeval = "eval" !utfw - '(' - e:eqs - ')' - { $$ = p5_evalstr_ast(P, G->lineno, P->line, e) }   # eval(EXPR) is a call: no operator after it is part of the argument
           | "eval" !utfw - !'{' e:eqs { $$ = p5_evalstr_ast(P, G->lineno, P->line, p5_unparen(P, e)) }
call = m:name - { $$ = PN_TUP(p5_qualify_call(P, m)) }
# $r->[i] / $h->{k} / $r->[0]{k}: tuple [base, key1, key2, ...]
# the base is a plain scalar or an element: $r->[0], $a[0]->[1], $h{k}->{j}
elemel = b:listel { $$ = b } | b:hashel { $$ = b }
elemchain = b:scalar k:elemstep1 { $$ = k = PN_PUSH(PN_TUP(b), k) }
            ( k2:elemstep { $$ = k = PN_PUSH(k, k2) } )*
          | '${' - b:eqs - '}' k:elemkey { $$ = k = PN_PUSH(PN_TUP(p5_deref_ref(P, b)), k) }   # ${$ref[1]}[0]: an element of the array the block returns
            ( k2:elemstep { $$ = k = PN_PUSH(k, k2) } )*
          # $a[0]{k} / $h{a}{b} / $a[0]->[1]: after an element the arrow between subscripts is
          # optional (one parse of the element, a second alternative on the base is exponential)
          | b:elemel k:elemstep { $$ = k = PN_PUSH(PN_TUP(b), k) }
            ( k2:elemstep { $$ = k = PN_PUSH(k, k2) } )*
          # [1,2]->[1] / {a=>1}->{a}: the element of an anonymous constructor
          | b:anonhash k:elemstep1 { $$ = k = PN_PUSH(PN_TUP(b), k) }
            ( k2:elemstep { $$ = k = PN_PUSH(k, k2) } )*
          | b:listref k:elemstep1 { $$ = k = PN_PUSH(PN_TUP(b), k) }
            ( k2:elemstep { $$ = k = PN_PUSH(k, k2) } )*
          # $$r[0] / ${$r}{k}: the element of the container REF points to ($r->[0])
          | '$' b:plainscalar k:elemkey { $$ = k = PN_PUSH(PN_TUP(b), k) }
            ( k2:elemstep { $$ = k = PN_PUSH(k, k2) } )*
          | '${' - b:plainscalar - '}' k:elemkey { $$ = k = PN_PUSH(PN_TUP(b), k) }
            ( k2:elemstep { $$ = k = PN_PUSH(k, k2) } )*
dscalar = '$' b:plainscalar { $$ = p5_deref(P, G->lineno, P->line, b) }
        | '$' d:dscalar { $$ = p5_deref(P, G->lineno, P->line, d) }
plainscalar = < '$' i:gid > - { $$ = PN_AST(MSG, p5_vn(P, "$", i)) }
elemstep1 = arrow k:elemkey { $$ = k }
elemstep = arrow? k:elemkey { $$ = k }
elemkey = '[' - i:eqs - ']' -  { $$ = i }
        | '{' - k:id - '}' -   { $$ = PN_AST(VALUE, k) }
        | '{' - k:eqs - '}' -  { $$ = k }
anonhash = !('{' - '{') '{' - s:listexprs - '}' -  { $$ = p5_anonhash(P, G->lineno, P->line, s) }
         | '{' - '}' -                { $$ = p5_anonhash(P, G->lineno, P->line, PN_NIL) }

# $cb->(args): call the closure in $cb. Same shape as a call of a local
# (MSG "$cb" with an arg LIST), which compile.c turns into
# getlocal/self/args/call.
p5coderef = k:p5coderef0 { $$ = k }
            ( arrow l2:list - { $$ = k = PN_TUP(p5_callv_node(P, G->lineno, P->line, PN_AST(EXPR, k), l2)); } )*
p5coderef0 = s:scalar arrow l:list -
            { if (PN_PART(s) == AST_MSG) { PN_SRC(s)->a[1] = PN_SRC(l); PN_TOUCH(s); $$ = PN_TUP(s); }
              else $$ = PN_TUP(p5_callv_node(P, G->lineno, P->line, s, l)) }
          | s:hashel arrow l:list -
            { $$ = PN_TUP(p5_callv_node(P, G->lineno, P->line, PN_AST(EXPR, PN_TUP(s)), l)); }
          | s:listel arrow l:list -
            { $$ = PN_TUP(p5_callv_node(P, G->lineno, P->line, PN_AST(EXPR, PN_TUP(s)), l)); }
          # &$c(ARGS) / &{$c}(ARGS) / &$c: call the code reference
          | '&' s:plainscalar l:list -
            { PN_SRC(s)->a[1] = PN_SRC(l); PN_TOUCH(s); $$ = PN_TUP(s) }
          | '&{' - s:plainscalar - '}' - l:list -
            { PN_SRC(s)->a[1] = PN_SRC(l); PN_TOUCH(s); $$ = PN_TUP(s) }
          | '&{' - e:eqs - '}' - l:list -
            { $$ = PN_TUP(p5_callv_node(P, G->lineno, P->line, e, l)) }
          | '&{' - e:eqs - '}' - &(semi | '}' | !. | ')')       # &{$c}; calls with the current @_
            { $$ = PN_PUSH(PN_TUP(e), PN_AST2(MSG, PN_STR("p5callv"), PN_AST(LIST, PN_TUP(PN_AST(EXPR, PN_TUP(PN_AST(MSG, PN_STR("@_")))))))) }
          | s:anonsub - arrow l:list -     # sub { ... }->(ARGS)
            { $$ = PN_TUP(p5_callv_node(P, G->lineno, P->line, s, l)) }
          | c:elemchain - '->' - l:list -
            { $$ = PN_TUP(p5_callv_node(P, G->lineno, P->line, p5_elem_chain(P, G->lineno, P->line, c, PN_NIL), l)) }
          | '&' i:id - !'(' &(semi | '}' | !. | ')')       # &name; calls name with the current @_
            { $$ = PN_PUSH(PN_TUP(PN_AST(MSG, i)),
                     PN_AST2(MSG, PN_STR("p5callv"),
                       PN_AST(LIST, PN_TUP(PN_AST(EXPR, PN_TUP(PN_AST(MSG, PN_STR("@_")))))))) }
          | '&' s:plainscalar - &(semi | '}' | !. | ')')
            { PN_SRC(s)->a[1] = PN_SRC(PN_AST(LIST, PN_NIL)); PN_TOUCH(s); $$ = PN_TUP(s) }
# exists $h{k} / exists $h->{k}: table "exists" method
p5exists = "exists" !utfw - '&' sn:qid -
           { $$ = PN_TUP(p5_sub_exists(P, G->lineno, P->line, p5_qname(P, sn))) }
         | "exists" !utfw - c:elemchain
           { $$ = PN_TUP(p5_elem_op(P, G->lineno, P->line, c, "p5exists", PN_NIL)) }
         | "exists" !utfw - '$' h:gid - '[' - i:eqs - ']' -
           { $$ = PN_PUSH(PN_TUP(PN_AST(MSG, p5_vn(P, "@", h))),
                          PN_AST2(MSG, PN_STR("p5exists"), PN_AST(LIST, PN_TUP(p5_index(P, i))))) }
         | "exists" !utfw - '$' h:id - '{' - k:value - '}' -
           { $$ = PN_PUSH(PN_TUP(PN_AST(MSG, p5_vn(P, "%", h))),
                          PN_AST2(MSG, PN_STR("exists"), PN_AST(LIST, PN_TUP(k)))) }
         | "exists" !utfw - '$' h:id - '{' - k:id - '}' -
           { $$ = PN_PUSH(PN_TUP(PN_AST(MSG, p5_vn(P, "%", h))),
                          PN_AST2(MSG, PN_STR("exists"), PN_AST(LIST, PN_TUP(PN_AST(VALUE, k))))) }
         | "exists" !utfw - '$^H' '{' - k:value - '}' -
           { $$ = PN_PUSH(PN_TUP(PN_AST(MSG, PN_STR("%^H"))), PN_AST2(MSG, PN_STR("exists"), PN_AST(LIST, PN_TUP(k)))) }
         | "exists" !utfw - '$^H' '{' - k:id - '}' -
           { $$ = PN_PUSH(PN_TUP(PN_AST(MSG, PN_STR("%^H"))), PN_AST2(MSG, PN_STR("exists"), PN_AST(LIST, PN_TUP(PN_AST(VALUE, k))))) }
# push/unshift @a, LIST: one push/unshift send per item, chained
pushop = < ("push" | "unshift") > !utfw - { $$ = PN_STRN(yytext, yyleng) }
p5push = o:pushop list-start - a:listvar - list-end -
           { $$ = p5_push(P, G->lineno, P->line, o, a, PN_TUP0()) }
       | o:pushop list-start - a:listvar - comma - l:listexprs - list-end -
           { $$ = p5_push(P, G->lineno, P->line, o, a, l) }
       | o:pushop a:listvar - comma - l:listexprs
           { $$ = p5_push(P, G->lineno, P->line, o, a, l) }
# splice(@a, OFF [, LEN [, LIST]]): in place, the value is the removed elements
splice-rest = comma - l:eqs - comma - r:listexprs { $$ = PN_PUSH(PN_PUSH(PN_TUP(p5_unparen(P, l)), PN_AST(EXPR, PN_TUP(PN_AST(LIST, r)))), PN_NIL) }
            | comma - l:eqs { $$ = PN_PUSH(PN_PUSH(PN_TUP(p5_unparen(P, l)), PN_AST(VALUE, PN_NIL)), PN_AST(VALUE, PN_NIL)) }
            | '' { $$ = PN_PUSH(PN_PUSH(PN_TUP(PN_AST(VALUE, PN_NIL)), PN_AST(VALUE, PN_NIL)), PN_AST(VALUE, PN_NIL)) }
p5splice = "splice" !utfw - list-start - a:listvar - comma - o:eqs - t:splice-rest list-end -
           { $$ = p5_splice_ast(P, G->lineno, P->line, a, o, t) }
         | "splice" !utfw - a:listvar - comma - o:eqs - t:splice-rest
           { $$ = p5_splice_ast(P, G->lineno, P->line, a, o, t) }
         | "splice" !utfw - list-start - a:listvar - list-end -
           { $$ = p5_splice_ast(P, G->lineno, P->line, a, PN_AST(VALUE, PN_ZERO), PN_PUSH(PN_PUSH(PN_TUP(PN_AST(VALUE, PN_NIL)), PN_AST(VALUE, PN_NIL)), PN_AST(VALUE, PN_NIL))) }
         | "splice" !utfw - a:listvar - &(semi | '}' | !. | ')')
           { $$ = p5_splice_ast(P, G->lineno, P->line, a, PN_AST(VALUE, PN_ZERO), PN_PUSH(PN_PUSH(PN_TUP(PN_AST(VALUE, PN_NIL)), PN_AST(VALUE, PN_NIL)), PN_AST(VALUE, PN_NIL))) }
p5delslice = "delete" !utfw - p:slicepair
           { PN so = p5_slice_op(P, G->lineno, P->line, PN_TUPLE_AT(p, 0), PN_TUPLE_AT(p, 1), PN_NIL);
             PN items = PN_S(so, 0), msg = PN_TUPLE_AT(items, PN_TUPLE_LEN(items) - 1);
             PN_SRC(msg)->a[0] = PN_SRC(PN_STR("p5slicedel")); PN_TOUCH(msg);
             $$ = PN_TUP(so) }
# delete $h{key}: send "delete" (removes key, returns the old value) to %h
# tie VAR, CLASS, LIST / tied VAR / untie VAR: the variable holds a Tie object (lib/p5/p5tie.c)
tvar = < '$' i:gid > -  { $$ = PN_AST(MSG, p5_vn(P, "$", i)) }
     | v:listvar { $$ = v }
     | v:hashvar { $$ = v }
p5tie = "tie" !utfw - '(' - MY? v:tvar - comma - cls:eqs - comma - a:listexprs - ')' -
        { $$ = p5_tie_ast(P, G->lineno, P->line, v, cls, a) }
      | "tie" !utfw - '(' - MY? v:tvar - comma - cls:eqs - ')' -
        { $$ = p5_tie_ast(P, G->lineno, P->line, v, cls, PN_NIL) }
      | "tie" !utfw - MY? v:tvar - comma - cls:eqs - comma - a:listexprs
        { $$ = p5_tie_ast(P, G->lineno, P->line, v, cls, a) }
      | "tie" !utfw - MY? v:tvar - comma - cls:eqs
        { $$ = p5_tie_ast(P, G->lineno, P->line, v, cls, PN_NIL) }
      | "tied" !utfw - '(' - v:tvar - ')' -
        { $$ = p5_fcall(P, G->lineno, P->line, "p5tobj", PN_TUP(v)) }
      | "tied" !utfw - v:tvar
        { $$ = p5_fcall(P, G->lineno, P->line, "p5tobj", PN_TUP(v)) }
      | "untie" !utfw - '(' - v:tvar - ')' -
        { $$ = p5_untie_ast(P, G->lineno, P->line, v) }
      | "untie" !utfw - v:tvar
        { $$ = p5_untie_ast(P, G->lineno, P->line, v) }
p5delete = "delete" !utfw - c:elemchain
           { $$ = PN_TUP(p5_elem_op(P, G->lineno, P->line, c, "p5delete", PN_NIL)) }
         | "delete" !utfw - '$' h:gid - '[' - i:eqs - ']' -
           { $$ = PN_PUSH(PN_TUP(PN_AST(MSG, p5_vn(P, "@", h))),
                          PN_AST2(MSG, PN_STR("p5delete"), PN_AST(LIST, PN_TUP(p5_index(P, i))))) }
         | "delete" !utfw - '$' h:id - '{' - k:value - '}' -
           { $$ = PN_PUSH(PN_TUP(PN_AST(MSG, p5_vn(P, "%", h))),
                          PN_AST2(MSG, PN_STR("delete"), PN_AST(LIST, PN_TUP(k)))) }
         | "delete" !utfw - '$' h:id - '{' - k:id - '}' -
           { $$ = PN_PUSH(PN_TUP(PN_AST(MSG, p5_vn(P, "%", h))),
                          PN_AST2(MSG, PN_STR("delete"), PN_AST(LIST, PN_TUP(PN_AST(VALUE, k))))) }
         | "delete" !utfw - '$^H' '{' - k:value - '}' -
           { $$ = PN_PUSH(PN_TUP(PN_AST(MSG, PN_STR("%^H"))), PN_AST2(MSG, PN_STR("delete"), PN_AST(LIST, PN_TUP(k)))) }
         | "delete" !utfw - '$^H' '{' - k:id - '}' -
           { $$ = PN_PUSH(PN_TUP(PN_AST(MSG, PN_STR("%^H"))), PN_AST2(MSG, PN_STR("delete"), PN_AST(LIST, PN_TUP(PN_AST(VALUE, k))))) }
# obj->m1(args)->m2->m3(args): one send per step. Name->new(k => v) of a known
# class is the constructor call Name(args) (p5_class_new).
# postfix dereference: $r->@*, $h->%*, $r->$#*, $r->@[LIST], $h->@{LIST}
methstep = arrow '@*' -             { $$ = PN_AST(MSG, PN_STR("p5aref")) }
       | arrow '%*' -               { $$ = PN_AST(MSG, PN_STR("p5href")) }
       | arrow '$#*' -              { $$ = PN_AST(MSG, PN_STR("p5lastidx")) }
       | arrow '@[' - l:listexprs - ']' -  { $$ = p5_sliceof(P, G->lineno, P->line, l) }
       | arrow '@{' - l:listexprs - '}' -  { $$ = p5_sliceof(P, G->lineno, P->line, l) }
       | arrow '$' n:gid - l:list -      # $o->$name(args): the method name (or code) is in $name
         { $$ = PN_AST3(MSG, PN_STR("p5dyn"), l, PN_AST(MSG, p5_vn(P, "$", n))) }
       | arrow '$' n:gid -
         { $$ = PN_AST3(MSG, PN_STR("p5dyn"), PN_NIL, PN_AST(MSG, p5_vn(P, "$", n))) }
       | arrow l:list -                     # $o->m(1)->(2): call the code a method returned
         { $$ = PN_AST2(MSG, PN_STR("p5callstep"), l) }
       | arrow m:name - l:list -
         { PN_SRC(m)->a[1] = PN_SRC(l); PN_TOUCH(m); $$ = m }
       | arrow m:name -
         { $$ = m }
method = v:methlhs - st:methstep
         { PN nw = p5_class_new(P, G->lineno, P->line, v, st);
           $$ = v = nw != PN_NIL ? PN_TUP(nw) : p5_meth_step(P, PN_TUPIF(p5_meth_recv(P, v)), st) }
         ( - st2:methstep { $$ = v = p5_meth_step(P, v, st2) } )*

name = !keyword m:qid -      { $$ = PN_AST(MSG, m) }
    | "::" !':' m:qid -     { $$ = PN_AST(MSG, p5_pkg[0] ? PN_STRCAT("main::", PN_STR_PTR(m)) : m) }  # ::name is main::name
     # &foo(...) / &foo: a call of the sub foo (subs are defined without the sigil)
     | !keyword '&' m:qid -  { $$ = PN_AST(MSG, m) }
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
# file handles: a bareword FH is the lobby global "*FH", a lexical one is a scalar
splitpat = x:regexp - { p5_re_g = 0; $$ = x }
         | e:eqs - { $$ = p5_split_awk(P, G->lineno, P->line, e) }
fhid = < [A-Za-z_] [A-Za-z0-9_]* > !utfw { $$ = PN_STRN(yytext, yyleng) }
fhname = i:fhid - { $$ = PN_AST(MSG, PN_STRCAT("*", PN_STR_PTR(i))) }
fhval = fhname | scalar
fhfn = "fileno" { $$ = PN_STR("fileno") } | "getc" { $$ = PN_STR("getc") }
dirop = "readdir" { $$ = PN_STR("p5readdir") } | "closedir" { $$ = PN_STR("p5closedir") } | "rewinddir" { $$ = PN_STR("p5rewinddir") }
fhtarget = MY i:scalar { $$ = i } | i:scalar { $$ = i }
         | &( < [A-Za-z_] [A-Za-z0-9_]* > &{ p5_fh_note(yytext) } ) fhname
# the handle of 'print FH LIST': a bareword, {$fh}, or a scalar followed by the list
fhidup = < [A-Z_] [A-Z0-9_]* > !utfw { $$ = PN_STRN(yytext, yyleng) }
fhprint = i:fhidup &(space+ ([\"'$@\\(] | !(("if" | "unless" | "for" | "foreach" | "while" | "until" | "and" | "or" | "xor" | "x" | "eq" | "ne" | "lt" | "gt" | "le" | "ge" | "cmp") !utfw) [a-zA-Z_]))
            { $$ = PN_AST(MSG, PN_STRCAT("*", PN_STR_PTR(i))) }
        | i:fhid &{ p5_fh_known(yytext) } &(space+ [\"']) { $$ = PN_AST(MSG, PN_STRCAT("*", PN_STR_PTR(i))) }
        | '{' - s:scalar - '}' - { $$ = s }
        | s:scalar &([\"'$@\\]) { $$ = s }
readline = '<>' - { $$ = PN_AST(EXPR, PN_TUP(PN_AST2(MSG, PN_STR("p5argvread"), PN_AST(LIST, PN_TUP(PN_AST(MSG, PN_STR("@ARGV"))))))) }
        | '<' h:fhval '>' -
        { $$ = PN_AST(EXPR, PN_PUSH(PN_TUP(h), PN_AST(MSG, PN_STR("p5readline")))) }
        | '<' < [-A-Za-z0-9_./~*?\[\]{},]+ > '>' &{ p5_globchars(yytext) } -   # <*.c> / <op/*>: a file glob
        { $$ = PN_AST(EXPR, PN_TUP(PN_AST2(MSG, PN_STR("glob"), PN_AST(LIST, PN_TUP(PN_AST(VALUE, PN_STRN(yytext, yyleng))))))) }
local-mark = '' { p5_local_blockstart(P); }
repl-mark = '' { if (p5_repl) p5_local_blockstart(P); }   # the input of p2 -d is one scope: `local` is restored at its end
lbblock = &{ p5_lb(1) } ( b:block &{ p5_lb(-1) } { $$ = b } | &{ p5_lb_fail() } )
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
          { $$ = PN_AST(EXPR, PN_PUSH(PN_TUP(p5_list_items(P, G->lineno, P->line, s)), PN_AST(MSG, PN_STR("clone")))) }
     | listref-start listref-end
          { $$ = PN_AST(EXPR, PN_PUSH(PN_TUP(PN_AST(LIST, PN_NIL)), PN_AST(MSG, PN_STR("clone")))) }
hash = hash-start h:hash-items - hash-end     { $$ = PN_AST(LIST, h) }
     | hash-start hash-end                    { $$ = PN_AST(LIST, PN_NIL) }

#path = '/' < utfw+ > - { $$ = PN_STRN(yytext, yyleng) }
#path    = < utfw+ > -  { $$ = PN_STRN(yytext, yyleng) }
#msg = < utfw+ > -   	{ $$ = PN_STRN(yytext, yyleng) }

mvalue = i:immed - { $$ = PN_AST(VALUE, i) }
      | "__PACKAGE__" !utfw - { $$ = p5_str_value(P, potion_str(P, p5_pkg[0] ? p5_pkg : "main")) }
       | global

balparen = '(' ( !'(' !')' . | balparen )* ')'   # a cheap scan, no semantics
methlhs = global
        | &(("shift" | "pop") !utfw - "->") u:p5unary { $$ = PN_AST(EXPR, PN_PUSH(PN_TUP(PN_AST(MSG, PN_STR("@_"))), u)) }   # shift->method
        | &(balparen - "->") l:list { $$ = p5_unparen(P, PN_AST(EXPR, PN_TUP(l))) }   # (shift)->method
        | "__PACKAGE__" !utfw - { $$ = p5_str_value(P, potion_str(P, p5_pkg[0] ? p5_pkg : "main")) }
        | pkgword
        | &["'] v:value &arrow { $$ = v }   # 'Class'->method, "Class"->method
        | name
# Foo::Bar:: is the string "Foo::Bar" (a package name that cannot be mistaken for a sub call)
pkgword = !keyword < IDFIRST utfw* ('::' IDFIRST utfw*)* > '::' ![A-Za-z_0-9:] -  { $$ = p5_str_value(P, PN_STRN(yytext, yyleng)) }

value = i:immed - { $$ = PN_AST(VALUE, i) }
      | e:str2 -   { $$ = e }
      | e:pkgword  { $$ = e }
      | e:qq -     { $$ = e }
      | e:qx -     { $$ = e }
      | e:bq -     { $$ = e }
      | global
      | listref
      | hash
immed = undef { $$ = PN_NIL }
#      | true  { $$ = PN_TRUE }
#      | false { $$ = PN_FALSE }
      | bin   { long cn; const char *c = p5_num_clean(yytext, yyleng, &cn); $$ = p5_int_lit(P, c, cn, 2) }
      | oct   { long cn; const char *c = p5_num_clean(yytext, yyleng, &cn); $$ = p5_int_lit(P, c, cn, 8) }
      | hex   { long cn; const char *c = p5_num_clean(yytext, yyleng, &cn); $$ = p5_int_lit(P, c, cn, 16) }
      | dec   { long cn; const char *c = p5_num_clean(yytext, yyleng, &cn);
                $$ = ($$ == YY_TDEC) ? potion_strtod(P, (char *)c, cn) : p5_int_lit(P, c, cn, 10) }
      | dec_wo_zero { $$ = potion_strtod(P, yytext, yyleng) }
      | str1

lexglobal = MY t:name i:global { PN_SRC(i)->a[2] = PN_SRC(t); PN_TOUCH(i); p5_lex_my = 1; $$ = i }
          | MY i:global        { p5_lex_my = 1; $$ = i }
          | OUR i:global       { p5_lex_my = 0; $$ = i }   # for our $i (..): the package variable
          | i:global           { p5_lex_my = 0; $$ = i }
# the variable of a foreach: `for my $i` is always a lexical, also at the top level of a p2 -d input (p5_vn)
lexglobalf = MYFOR t:name i:global { PN_SRC(i)->a[2] = PN_SRC(t); PN_TOUCH(i); p5_lex_my = 1; $$ = i }
          | MYFOR i:global     { p5_lex_my = 1; $$ = i }
          | OUR i:global       { p5_lex_my = 0; $$ = i }
          | i:global           { p5_lex_my = 0; $$ = i }

global  = scalar | slice | listvar | hashvar | listel | hashel | funcvar | globvar
# @a[LIST], @h{LIST}, @$r[LIST]: (indices).p5slice(container)
slice   = p:slicepair { $$ = p5_slice(P, G->lineno, P->line, PN_TUPLE_AT(p, 0), PN_TUPLE_AT(p, 1)) }
# the (container name, index list) of a slice
slicepair = '@' i:gid - '[' - l:listexprs - ']' -
            { $$ = PN_PUSH(PN_TUP(p5_vn(P, "@", i)), l) }
        | '@' '$' i:gid - '[' - l:listexprs - ']' -
            { $$ = PN_PUSH(PN_TUP(p5_vn(P, "$", i)), l) }
        | '@' i:gid - '{' - l:listexprs - '}' -
            { $$ = PN_PUSH(PN_TUP(p5_vn(P, "%", i)), l) }
        | '@' '$' i:gid - '{' - l:listexprs - '}' -
            { $$ = PN_PUSH(PN_TUP(p5_vn(P, "$", i)), l) }
        | '@{' - '$' i:gid - '}' - '[' - l:listexprs - ']' -
            { $$ = PN_PUSH(PN_TUP(p5_vn(P, "$", i)), l) }
        | '@{' - '$' i:gid - '}' - '{' - l:listexprs - '}' -
            { $$ = PN_PUSH(PN_TUP(p5_vn(P, "$", i)), l) }
# special scalar vars
specialcaratscalar = < '^' [A-Z] >
specialscalar = < '$' ( [@%!"$()0<>&`'+|/,.;?\\\]~] | specialcaratscalar | '^' ) > # "
  { $$ = PN_STRN(yytext, yyleng) }  # without it 'i:specialscalar' read a stale slot
# send the value a msg, every global is a closure (see name)
scalar  = < '$' [1-9] [0-9]* > - !'[' !'{'     # $N: group N of the last match
	  { $$ = PN_AST2(MSG, PN_STR("@^M"),
	                 PN_AST(LIST, PN_TUP(PN_AST(VALUE, PN_NUM(PN_ATOI(yytext + 1, yyleng - 1, 10)))))) }
	| < '$' i:gid > - !'[' !'{'
	  { $$ = p5_sread(P, G->lineno, P->line, p5_vn(P, "$", i)) }
	# ${^NAME}: a special variable with a long name (${^UTF8CACHE}, ${^OPEN}), a plain global here
	| '${^' < [A-Z] [A-Z0-9_]* > '}' - !'[' !'{'
	  { if (!strcmp(yytext, "GLOBAL_PHASE")) { p5_end_root(P); p5_phase_used = 1; }
	    $$ = PN_AST(MSG, PN_STRCAT("$^", yytext)) }
	# $$r / ${$r}: the scalar a reference points to
	| d:dscalar !'[' !'{' { $$ = d }
	| < '${' - i:gid - '}' > - !'[' !'{'     # ${name}: the scalar $name
	  { $$ = p5_sread(P, G->lineno, P->line, p5_vn(P, "$", i)) }
	| '${' - b:plainscalar - '}' - !'[' !'{' { $$ = p5_deref(P, G->lineno, P->line, b) }
	| '${' - e:eqs - '}' - !'[' !'{' { $$ = p5_deref(P, G->lineno, P->line, e) }   # ${ EXPR }: the scalar a reference points to
	| i:specialscalar - !'[' !'{'
	  { if (PN_STR_PTR(i)[0] == '^') i = PN_STRCAT("$", PN_STR_PTR(i)); /* the nested capture of specialcaratscalar replaced yytext */
	    $$ = !strcmp(PN_STR_PTR(i), "$^H") ? p5_hints_read(P, G->lineno, P->line) : PN_AST(MSG, i) }
	# $#$r / $#{$r}: last index of the array REF points to
	| '$#$' i:gid -
	  { $$ = PN_OP(AST_MINUS, PN_AST(EXPR, PN_PUSH(PN_TUP(PN_AST(MSG, p5_vn(P, "$", i))), PN_AST(MSG, PN_STR("length")))),
	               PN_AST(EXPR, PN_TUP(PN_AST(VALUE, PN_NUM(1))))) }
	| '$#{' - '$' i:gid - '}' -
	  { $$ = PN_OP(AST_MINUS, PN_AST(EXPR, PN_PUSH(PN_TUP(PN_AST(MSG, p5_vn(P, "$", i))), PN_AST(MSG, PN_STR("length")))),
	               PN_AST(EXPR, PN_TUP(PN_AST(VALUE, PN_NUM(1))))) }
	# $#+ / $#-: last index of the @+ / @- match offset arrays
	| '$#' ( '+' | '-' ) -
	  { $$ = PN_OP(AST_MINUS,
                   PN_AST(EXPR, PN_PUSH(PN_TUP(PN_AST(MSG, PN_STR(yytext[0] == '+' ? "@+" : "@-"))),
	                                       PN_AST(MSG, PN_STR("length")))),
	               PN_AST(EXPR, PN_TUP(PN_AST(VALUE, PN_NUM(1))))) }
	# size of array
	| < '$' '#' l:id >  -
	  { $$ = PN_OP(AST_MINUS, 
                   PN_AST(EXPR, PN_PUSH(PN_TUP(PN_AST(MSG, p5_vn(P, "@", l))),
	                                       PN_AST(MSG, PN_STR("length")))),
                   PN_AST(EXPR, PN_TUP(PN_AST(VALUE, PN_NUM(1))))) }
listvar = < '@' i:gid > - { PN vn = p5_vn(P, "@", i); p5_see(PN_STR_PTR(vn), PN_STR_LEN(vn)); $$ = PN_AST(MSG, vn); }
        # @+ / @-: the match offset arrays of the last match
        | '@' ( '+' | '-' ) - { $$ = PN_AST(MSG, PN_STR(yytext[0] == '-' ? "@-" : "@+")) }
        # @$r / @{$r}: arrays are tuples, so the reference already is the array
        | '@' '$' i:gid - { $$ = p5_aref(P, G->lineno, P->line, PN_AST(MSG, p5_vn(P, "$", i))) }
        | '@{' - '$' i:gid - '}' - { $$ = p5_aref(P, G->lineno, P->line, PN_AST(MSG, p5_vn(P, "$", i))) }
        # @{ EXPR }: arrays are tuples, the reference already is the array
        | '@{' - e:eqs - '}' - { $$ = p5_aref(P, G->lineno, P->line, e) }
hashvar = '%+' - { $$ = PN_AST(MSG, PN_STR("%+")) }
        | '%^H' !utfw - { $$ = PN_AST(MSG, PN_STR("%^H")) }
        | '%' < ("ENV" | "Config" | "INC") > !utfw - { p5_env_touch(P); $$ = PN_AST(MSG, PN_STRCAT("%", yytext)) }
        | < '%' i:gid > - { PN vn = p5_vn(P, "%", i); p5_see(PN_STR_PTR(vn), PN_STR_LEN(vn)); $$ = PN_AST(MSG, vn); }
        | '%' '$' i:gid - { $$ = PN_AST(MSG, p5_vn(P, "$", i)) }
        | '%{' - '$' i:gid - '}' - { $$ = PN_AST(MSG, p5_vn(P, "$", i)) }
        | '%{' - e:eqs - '}' - { $$ = e }
funcvar = < '&' i:id > - { $$ = PN_AST(MSG, PN_STRCAT("&", PN_STR_PTR(i))) }
globvar = < '*' i:id > - { $$ = PN_AST(MSG, PN_STRCAT("*", PN_STR_PTR(i))) }
listel  = < '$-' '[' - i:eqs - ']' > -          # $-[N], $+[N]: where group N of the last match started / ended
          { $$ = PN_AST2(MSG, PN_STR("@-"), PN_AST(LIST, PN_TUP(p5_index(P, i)))) }
        | < '$+' '[' - i:eqs - ']' > -
          { $$ = PN_AST2(MSG, PN_STR("@+"), PN_AST(LIST, PN_TUP(p5_index(P, i)))) }
        # a space before the bracket is fine too
        | '$-' - '[' - i:eqs - ']' -
          { $$ = PN_AST2(MSG, PN_STR("@-"), PN_AST(LIST, PN_TUP(p5_index(P, i)))) }
        | '$+' - '[' - i:eqs - ']' -
          { $$ = PN_AST2(MSG, PN_STR("@+"), PN_AST(LIST, PN_TUP(p5_index(P, i)))) }
        | < '$' l:gid - '[' - i:eqs - ']' > -
	  { $$ = p5_tel(P, G->lineno, P->line, p5_vn(P, "@", l), PN_AST(LIST, PN_TUP(p5_index(P, i)))) }
	# ?? used as $#[0] in base/lex.t
	| < '$' '#' - '[' - i:value - ']' >  -
	  { $$ = PN_AST2(MSG, PN_STR("@_"), PN_AST(LIST, PN_TUP(i))) }
hashel  = < '$+' '{' - k:id - '}' > -          # $+{name}: the named group of the last match
          { $$ = PN_AST2(MSG, PN_STR("%+"), PN_AST(LIST, PN_TUP(PN_AST(VALUE, k)))) }
        | < '$+' '{' - k:eqs - '}' > -
          { $$ = PN_AST2(MSG, PN_STR("%+"), PN_AST(LIST, PN_TUP(k))) }
        | < '$!' '{' - k:id - '}' > -          # $!{ENOENT}: is $! that error
          { $$ = PN_AST2(MSG, PN_STR("p5errnoname"), PN_AST(LIST, PN_TUP(PN_AST(VALUE, k)))) }
        | < '$^H' '{' - k:id - '}' > -          # $^H{name}: the hints hash %^H, a plain global hash here
          { $$ = p5_tel(P, G->lineno, P->line, PN_STR("%^H"), PN_AST(LIST, PN_TUP(PN_AST(VALUE, k)))) }
        | < '$^H' '{' - k:eqs - '}' > -
          { $$ = p5_tel(P, G->lineno, P->line, PN_STR("%^H"), PN_AST(LIST, PN_TUP(k))) }
        | < '$' h:gid - '{' - k:eqs - comma - ks:listexprs - '}' > -      # $h{$a, $b}: the key is join($;, LIST)
          { PN items = PN_PUSH(PN_TUP(p5_unparen(P, k)), PN_NIL), key;
            PN_SIZE ki;
            items = PN_TUP(p5_unparen(P, k));
            for (ki = 0; ki < PN_TUPLE_LEN(ks); ki++) items = PN_PUSH(items, p5_unparen(P, PN_TUPLE_AT(ks, ki)));
            key = PN_AST(EXPR, PN_PUSH(PN_TUP(PN_AST(LIST, items)), PN_AST2(MSG, PN_STR("p5joins"),
                    PN_AST(LIST, PN_TUP(PN_AST(VALUE, PN_STR("\034")))))));
            $$ = p5_tel(P, G->lineno, P->line, p5_vn(P, "%", h), PN_AST(LIST, PN_TUP(key))) }
        | < '$' h:gid - '{' - k:id - '}' > -
          { $$ = p5_tel(P, G->lineno, P->line, p5_vn(P, "%", h), PN_AST(LIST, PN_TUP(PN_AST(VALUE, k)))) }
        | < '$' h:gid - '{' - k:eqs - '}' > -
          { $$ = p5_tel(P, G->lineno, P->line, p5_vn(P, "%", h), PN_AST(LIST, PN_TUP(k))) }

semi = ';' { p5_in_my = 0; }
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
assign = '=' !'~' - { p5_in_my = 0; }
defassign = ":=" --
# one parse of an element for ++/--: elemchain, a hash or an array element
elemlike = c:elemchain { $$ = p5_elem_chain(P, G->lineno, P->line, c, PN_NIL) } | h:hashel { $$ = h } | h:listel { $$ = h }
incop = pplus { $$ = PN_NUM(1) } | mminus { $$ = PN_NUM(-1) }
pplus = "++" -
mminus = "--" -
minus = '-' -
plus = '+' -
dot = '.' !'.' -
# repetition: 'x' as a word (not x=>, x=, or a longer identifier)
rep = 'x' !IDFIRST !'=' !fatcomma -
times = '*' -
div = '/' !'/' -
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
strlt = "lt" !utfw --
strgt = "gt" !utfw --
strle = "le" !utfw --
strge = "ge" !utfw --
numeq  = "==" --
strneq = "ne" !utfw --
numcmp = "<=>" --
strcmpop = "cmp" !utfw --
p5nunary = <( "length" | "ord" | "abs" | "chr" | "ucfirst" | "lcfirst" | "lc" | "uc" | "ref" | "int" | "sqrt" | "sin" | "cos" | "exp" | "log" | "hex" | "oct" | "sleep" | "quotemeta" )> !utfw - { $$ = PN_AST(MSG, PN_STRN(yytext, yyleng)) }
# the comparator name of 'sort NAME LIST': a bareword that is no builtin, followed by the list
sortsub = !(("keys" | "values" | "map" | "grep" | "reverse" | "sort" | "split" | "qw" | "my" | "do" | "uniq" | "each"
            | "lc" | "uc" | "scalar" | "join" | "sprintf" | "shift" | "pop" | "splice" | "glob" | "readdir" | "ucfirst"
            | "lcfirst" | "sub" | "values" | "delete" | "exists" | "defined" | "ref" | "length" | "abs" | "int" | "hex" | "oct") !utfw)
          i:fhid - &('@' | '(' | '$' | [0-9"'] | "keys" | "values" | "map" | "grep" | "reverse" | "qw")
          { $$ = i }
p5unary = <( "length" | "ord" | "abs" | "chr" | "shift" | "pop" | "keys" | "values" | "each" | "int" | "sqrt" | "sin" | "cos" | "exp" | "log" | "hex" | "oct" | "sleep" | "ucfirst" | "lcfirst" | "lc" | "uc" | "reverse" | "ref" )> !utfw - { $$ = PN_AST(MSG, (yyleng == 4 && !strncmp(yytext, "each", 4)) ? PN_STR("p5each") : PN_STRN(yytext, yyleng)) }
and = ("&&" | "and" !utfw) --
or = ("||" | "or" !utfw) --
not = ("!" | "not" !utfw) --
# only compiler specific keywords
keyword = (("and" | "or" | "not" | "sub" | "return" | "quasi") !utfw)

ftletter = < [edfrwxoRWXOszlpSbcugktTBMAC] > { $$ = PN_STRN(yytext, yyleng) }

undef = "undef" !utfw
#true = "true" !utfw
#false = "false" !utfw
hexl = [0-9A-Fa-f]
hex = '0' [xX] < hexl (hexl | '_')* >
bin = '0' [bB] < [01] [01_]* >
oct = '0' < [0-7_] [0-7_]* > ![0-9.eE]
# TODO allow _
dec = < !('-' '0' [0-9xXbB_]) '-'? ('0' | [1-9][0-9_]* )
        ('.' [0-9] [0-9_]* { $$ = YY_TDEC } | '.' ![.0-9A-Za-z_=] { $$ = YY_TDEC })?
        ([eE] [-+]? [0-9]+ { $$ = YY_TDEC })? >
dec_wo_zero = < '-'? '.' [0-9]+ >
version = 'v'? < ('0' | [1-9][0-9]*) ('.' [0-9]+ { $$ = YY_TDEC })? >
          { $$ = ($$ == YY_TDEC) ? PN_STRN(yytext, yyleng)
                                 : PN_NUM(PN_ATOI(yytext, yyleng, 10)) }

# ---- delimited regex forms: m{..} m!..! qr|..| s{..}{..} s!..!..! tr/../../ y/../../ ----
rxb_d0 = < ('\\' . | !'/' .)* > { $$ = PN_STRN(yytext, yyleng) }
rxm_d0 = '/' a:rxb_d0 '/' { $$ = a }
rxs_d0 = '/' a:rxb_d0 '/' b:rxb_d0 '/' { $$ = PN_PUSH(PN_TUP(a), b) }
rxb_d1 = < ('\\' . | !'!' .)* > { $$ = PN_STRN(yytext, yyleng) }
rxm_d1 = '!' a:rxb_d1 '!' { $$ = a }
rxs_d1 = '!' a:rxb_d1 '!' b:rxb_d1 '!' { $$ = PN_PUSH(PN_TUP(a), b) }
rxb_d2 = < ('\\' . | !'|' .)* > { $$ = PN_STRN(yytext, yyleng) }
rxm_d2 = '|' a:rxb_d2 '|' { $$ = a }
rxs_d2 = '|' a:rxb_d2 '|' b:rxb_d2 '|' { $$ = PN_PUSH(PN_TUP(a), b) }
rxb_d3 = < ('\\' . | !'#' .)* > { $$ = PN_STRN(yytext, yyleng) }
rxm_d3 = '#' a:rxb_d3 '#' { $$ = a }
rxs_d3 = '#' a:rxb_d3 '#' b:rxb_d3 '#' { $$ = PN_PUSH(PN_TUP(a), b) }
rxb_d4 = < ('\\' . | !'~' .)* > { $$ = PN_STRN(yytext, yyleng) }
rxm_d4 = '~' a:rxb_d4 '~' { $$ = a }
rxs_d4 = '~' a:rxb_d4 '~' b:rxb_d4 '~' { $$ = PN_PUSH(PN_TUP(a), b) }
rxb_d5 = < ('\\' . | !'^' .)* > { $$ = PN_STRN(yytext, yyleng) }
rxm_d5 = '^' a:rxb_d5 '^' { $$ = a }
rxs_d5 = '^' a:rxb_d5 '^' b:rxb_d5 '^' { $$ = PN_PUSH(PN_TUP(a), b) }
rxb_d6 = < ('\\' . | !'%' .)* > { $$ = PN_STRN(yytext, yyleng) }
rxm_d6 = '%' a:rxb_d6 '%' { $$ = a }
rxs_d6 = '%' a:rxb_d6 '%' b:rxb_d6 '%' { $$ = PN_PUSH(PN_TUP(a), b) }
rxb_d7 = < ('\\' . | !'@' .)* > { $$ = PN_STRN(yytext, yyleng) }
rxm_d7 = '@' a:rxb_d7 '@' { $$ = a }
rxs_d7 = '@' a:rxb_d7 '@' b:rxb_d7 '@' { $$ = PN_PUSH(PN_TUP(a), b) }
rxb_d8 = < ('\\' . | !':' .)* > { $$ = PN_STRN(yytext, yyleng) }
rxm_d8 = ':' a:rxb_d8 ':' { $$ = a }
rxs_d8 = ':' a:rxb_d8 ':' b:rxb_d8 ':' { $$ = PN_PUSH(PN_TUP(a), b) }
rxb_d9 = < ('\\' . | !';' .)* > { $$ = PN_STRN(yytext, yyleng) }
rxm_d9 = ';' a:rxb_d9 ';' { $$ = a }
rxs_d9 = ';' a:rxb_d9 ';' b:rxb_d9 ';' { $$ = PN_PUSH(PN_TUP(a), b) }
rxb_d10 = < ('\\' . | !'"' .)* > { $$ = PN_STRN(yytext, yyleng) }
rxm_d10 = '"' a:rxb_d10 '"' { $$ = a }
rxs_d10 = '"' a:rxb_d10 '"' b:rxb_d10 '"' { $$ = PN_PUSH(PN_TUP(a), b) }
rxb_d11 = < ('\\' . | !['] .)* > { $$ = PN_STRN(yytext, yyleng) }
rxm_d11 = ['] a:rxb_d11 ['] { $$ = a }
rxs_d11 = ['] a:rxb_d11 ['] b:rxb_d11 ['] { $$ = PN_PUSH(PN_TUP(a), b) }
rxb_d12 = < ('\\' . | !',' .)* > { $$ = PN_STRN(yytext, yyleng) }
rxm_d12 = ',' a:rxb_d12 ',' { $$ = a }
rxs_d12 = ',' a:rxb_d12 ',' b:rxb_d12 ',' { $$ = PN_PUSH(PN_TUP(a), b) }
rxb_q = < ('\\' . | !'?' .)* > { $$ = PN_STRN(yytext, yyleng) }
rxm_q = '?' a:rxb_q '?' { $$ = a }
rxn_p0 = ( '\\' . | '{' rxn_p0 '}' | !'{' !'}' . )*
rxb_p0 = < rxn_p0 > { $$ = PN_STRN(yytext, yyleng) }
rxm_p0 = '{' a:rxb_p0 '}' { $$ = a }
rxs_p0 = '{' a:rxb_p0 '}' - '{' b:rxb_p0 '}' { $$ = PN_PUSH(PN_TUP(a), b) }
rxn_p1 = ( '\\' . | '[' rxn_p1 ']' | !'[' !']' . )*
rxb_p1 = < rxn_p1 > { $$ = PN_STRN(yytext, yyleng) }
rxm_p1 = '[' a:rxb_p1 ']' { $$ = a }
rxs_p1 = '[' a:rxb_p1 ']' - '[' b:rxb_p1 ']' { $$ = PN_PUSH(PN_TUP(a), b) }
rxn_p2 = ( '\\' . | '(' rxn_p2 ')' | !'(' !')' . )*
rxb_p2 = < rxn_p2 > { $$ = PN_STRN(yytext, yyleng) }
rxm_p2 = '(' a:rxb_p2 ')' { $$ = a }
rxs_p2 = '(' a:rxb_p2 ')' - '(' b:rxb_p2 ')' { $$ = PN_PUSH(PN_TUP(a), b) }
rxn_p3 = ( '\\' . | '<' rxn_p3 '>' | !'<' !'>' . )*
rxb_p3 = < rxn_p3 > { $$ = PN_STRN(yytext, yyleng) }
rxm_p3 = '<' a:rxb_p3 '>' { $$ = a }
rxs_p3 = '<' a:rxb_p3 '>' - '<' b:rxb_p3 '>' { $$ = PN_PUSH(PN_TUP(a), b) }
rxmbody = rxm_d0 | rxm_d1 | rxm_d2 | rxm_d3 | rxm_d4 | rxm_d5 | rxm_d6 | rxm_d7 | rxm_d8 | rxm_d9 | rxm_d10 | rxm_d11 | rxm_d12 | rxm_p0 | rxm_p1 | rxm_p2 | rxm_p3
rxsbody = rxs_d0 | rxs_d1 | rxs_d2 | rxs_d3 | rxs_d4 | rxs_d5 | rxs_d6 | rxs_d7 | rxs_d8 | rxs_d9 | rxs_d10 | rxs_d11 | rxs_d12 | rxs_p0 | rxs_p1 | rxs_p2 | rxs_p3
rxflags = < [imsxgc]* > { $$ = PN_STRN(yytext, yyleng) }
rxsflags = < [gimsxer]* > { $$ = PN_STRN(yytext, yyleng) }
rxtflags = < [cdsr]* > { $$ = PN_STRN(yytext, yyleng) }
rxm = "m" !utfw a:rxmbody f:rxflags { $$ = p5_re_lit(P, G->lineno, a, f) }
    | "m" !utfw q:rxm_q f:rxflags { $$ = p5_re_once(P, G->lineno, q, f) }
rxqr = "qr" !utfw a:rxmbody f:rxflags { $$ = p5_re_lit(P, G->lineno, a, f) }
# s/PAT/REPL/flags and tr/FROM/TO/flags: (pattern, replacement, flags)
rxs = "s" !utfw a:rxsbody f:rxsflags { $$ = PN_PUSH(a, f) }
rxt = ("tr" | "y") !utfw a:rxsbody f:rxtflags { $$ = PN_PUSH(a, f) }
# qr/PAT/flags: the (?flags)PAT string; usable as the rhs of =~
qrexp = x:rxqr - { p5_re_g = 0; $$ = x }
regexp = x:rxm - { $$ = x }
       | < '/' ('\\' . | [^/\r\n])* '/' [imsxgc]* > -
         { $$ = p5_regexp(P, G->lineno, yytext, yyleng); }
       | < '/' ('\\' . | [^/])* '/' [imsgc]* 'x' [imsxgc]* > -
         { $$ = p5_regexp(P, G->lineno, yytext, yyleng); }

q1 = [']   # ' emacs highlight problems
c1 = < (!q1 !esc utf8)+ > { P->pbuf = potion_asm_write(P, P->pbuf, yytext, yyleng) }
c1b = esc { P->pbuf = potion_asm_write(P, P->pbuf, "\\", 1) }
str1 = q1 { P->pbuf = potion_asm_clear(P, P->pbuf) }
       < (esc q1 { P->pbuf = potion_asm_write(P, P->pbuf, "'", 1) }
         | esc esc { P->pbuf = potion_asm_write(P, P->pbuf, "\\", 1) }
         | c1b | c1)* >
       q1 { $$ = potion_bytes_string(P, PN_NIL, (PN)P->pbuf) }

esc         = '\\'
escn        = esc 'n' { P->pbuf = potion_asm_write(P, P->pbuf, "\n", 1) }
escb        = esc 'b' { P->pbuf = potion_asm_write(P, P->pbuf, "\b", 1) }
escf        = esc 'f' { P->pbuf = potion_asm_write(P, P->pbuf, "\f", 1) }
escr        = esc 'r' { P->pbuf = potion_asm_write(P, P->pbuf, "\r", 1) }
esct        = esc 't' { P->pbuf = potion_asm_write(P, P->pbuf, "\t", 1) }
escu        = esc 'u' < hexl hexl hexl hexl > { p5_put_cp(P, PN_ATOI(yytext, yyleng, 16)) }
# \x41 \x{263a} \N{U+263A} \033 \0 \101 \e \a \cA: a code point, UTF-8 encoded like the strings are
escx        = esc 'x' '{' < (!'}' utf8)* > '}'  { p5_put_numbrace(P, yytext, yyleng, 16) }
            | esc 'o{' < (!'}' utf8)* > '}'     { p5_put_numbrace(P, yytext, yyleng, 8) }
            | esc 'x' < hexl hexl? >     { p5_put_cp(P, PN_ATOI(yytext, yyleng, 16)) }
            | esc 'x' !hexl              { p5_put_cp(P, 0) }
            | esc 'N{U+' < hexl+ > '}'   { p5_put_cp(P, PN_ATOI(yytext, yyleng, 16)) }
            | esc < ( '0' [0-7]? [0-7]? | [1-7] [0-7] [0-7] ) > { { unsigned long o = 0; int oi; for (oi = 0; oi < yyleng; oi++) o = o * 8 + (yytext[oi] - '0'); p5_put_cp(P, o); } }
            | esc 'e'                    { p5_put_cp(P, 27) }
            | esc 'a'                    { p5_put_cp(P, 7) }
            | esc 'c' '\\' '\\'          { p5_put_cp(P, 28) }
            | esc 'c' < [@A-Za-z\[\\\]^_?] > { p5_put_cp(P, yytext[0] == '?' ? 127 : (toupper((unsigned char)yytext[0]) ^ 64)) }
# \Q \U \L \E: a marker piece, p5_dq_join wraps the pieces up to the \E in quotemeta/uc/lc
escm = esc < [QULEul] > {
  char mk[2] = { 1, yytext[0] };
  P->dqpieces = PN_PUSH(P->dqpieces, PN_AST(VALUE, potion_bytes_string(P, PN_NIL, (PN)P->pbuf)));
  P->dqpieces = PN_PUSH(P->dqpieces, PN_AST(VALUE, PN_STRN(mk, 2)));
  P->pbuf = potion_asm_clear(P, P->pbuf);
}
escc = esc < utf8 > { P->pbuf = potion_asm_write(P, P->pbuf, yytext, yyleng) }

q2 = ["]
e2 = '\\' ["] { P->pbuf = potion_asm_write(P, P->pbuf, "\"", 1) }
c2 = < (!q2 !esc !(('$' (IDFIRST | '::' IDFIRST | '$' IDFIRST | '{^' [A-Z] | '{$' IDFIRST | [1-9] | '0' ![0-9] | '&' | '@' | '!' | '?' | '^' [A-Z] | ',' | ';' | '.' | '/' | '$' !'{' | '+' [{\[] | '-' '[' | '#' (IDFIRST | '$' IDFIRST | '{$' IDFIRST))) | ('@' (IDFIRST | '$' IDFIRST | '{['))) utf8)+ > { P->pbuf = potion_asm_write(P, P->pbuf, yytext, yyleng) }
# "$a[1]" / "$a[$i]" / "$h{key}" / "$h{$k}": subscripted interpolation,
# same AST as the listel/hashel code rules but without their trailing
# whitespace skipping (which would eat literal spaces in the string).
dqel = '$' n:dqid '[' - i:mvalue - ']' {
  P->dqpieces = PN_PUSH(P->dqpieces, PN_AST(VALUE, potion_bytes_string(P, PN_NIL, (PN)P->pbuf)));
  P->dqpieces = PN_PUSH(P->dqpieces, p5_tel(P, G->lineno, P->line, p5_vn(P, "@", n), PN_AST(LIST, PN_TUP(i))));
  P->pbuf = potion_asm_clear(P, P->pbuf);
}
     | '$' n:dqid '{' - k:id - '}' {
  P->dqpieces = PN_PUSH(P->dqpieces, PN_AST(VALUE, potion_bytes_string(P, PN_NIL, (PN)P->pbuf)));
  P->dqpieces = PN_PUSH(P->dqpieces, p5_tel(P, G->lineno, P->line, p5_vn(P, "%", n), PN_AST(LIST, PN_TUP(PN_AST(VALUE, k)))));
  P->pbuf = potion_asm_clear(P, P->pbuf);
}
     | '$' n:dqid '{' - k:mvalue - '}' {
  P->dqpieces = PN_PUSH(P->dqpieces, PN_AST(VALUE, potion_bytes_string(P, PN_NIL, (PN)P->pbuf)));
  P->dqpieces = PN_PUSH(P->dqpieces, p5_tel(P, G->lineno, P->line, p5_vn(P, "%", n), PN_AST(LIST, PN_TUP(k))));
  P->pbuf = potion_asm_clear(P, P->pbuf);
}
# "$r->{k}", "$r->[0]", "$r->{a}{b}[1]", "$h{a}{b}", "$a[0][1]": element chains in a string
# (no whitespace skipping, a literal space after the } stays)
dqkey = '[' i:eqs ']' { $$ = i }
      | '{' k:id '}' { $$ = PN_AST(VALUE, k) }
      | '{' k:eqs '}' { $$ = k }
dqchain = '$' n:dqid '->' k:dqkey { $$ = k = PN_PUSH(PN_TUP(PN_AST(MSG, p5_vn(P, "$", n))), k) }
          ( '->'? k2:dqkey { $$ = k = PN_PUSH(k, k2) } )*
          { P->dqpieces = PN_PUSH(P->dqpieces, PN_AST(VALUE, potion_bytes_string(P, PN_NIL, (PN)P->pbuf)));
            P->dqpieces = PN_PUSH(P->dqpieces, p5_elem_chain(P, G->lineno, P->line, k, PN_NIL));
            P->pbuf = potion_asm_clear(P, P->pbuf); }
        | '$' n:dqid '{' k0:dqkeyin '}' k:dqkey2 { $$ = k = PN_PUSH(PN_TUP(PN_AST2(MSG, p5_vn(P, "%", n), PN_AST(LIST, PN_TUP(k0)))), k) }
          ( '->'? k2:dqkey { $$ = k = PN_PUSH(k, k2) } )*
          { P->dqpieces = PN_PUSH(P->dqpieces, PN_AST(VALUE, potion_bytes_string(P, PN_NIL, (PN)P->pbuf)));
            P->dqpieces = PN_PUSH(P->dqpieces, p5_elem_chain(P, G->lineno, P->line, k, PN_NIL));
            P->pbuf = potion_asm_clear(P, P->pbuf); }
        | '$' n:dqid '[' k0:dqidx ']' k:dqkey2 { $$ = k = PN_PUSH(PN_TUP(PN_AST2(MSG, p5_vn(P, "@", n), PN_AST(LIST, PN_TUP(k0)))), k) }
          ( '->'? k2:dqkey { $$ = k = PN_PUSH(k, k2) } )*
          { P->dqpieces = PN_PUSH(P->dqpieces, PN_AST(VALUE, potion_bytes_string(P, PN_NIL, (PN)P->pbuf)));
            P->dqpieces = PN_PUSH(P->dqpieces, p5_elem_chain(P, G->lineno, P->line, k, PN_NIL));
            P->pbuf = potion_asm_clear(P, P->pbuf); }
dqkeyin = k:id { $$ = PN_AST(VALUE, k) } | k:eqs { $$ = k }
dqidx = i:eqs { $$ = i }
dqkey2 = '->'? k:dqkey { $$ = k }
dqvar = dqcaret | dqchain | dqexpr2 | dqel | dqmatch | dqmvar | dqlastref | dqlastref2 | dqslice | dqexpr | dqlastidx | dqderef3 | dqderef2 | dqderef | dqspecial | dqbrace | dqscalar | dqarray2 | dqarray
# "$#a": last index; "@a": the elements joined with a space
dqlastidx = '$#' n:dqid {
  P->dqpieces = PN_PUSH(P->dqpieces, PN_AST(VALUE, potion_bytes_string(P, PN_NIL, (PN)P->pbuf)));
  P->dqpieces = PN_PUSH(P->dqpieces, PN_OP(AST_MINUS,
      PN_AST(EXPR, PN_PUSH(PN_TUP(PN_AST(MSG, p5_vn(P, "@", n))), PN_AST(MSG, PN_STR("length")))),
      PN_AST(EXPR, PN_TUP(PN_AST(VALUE, PN_NUM(1))))));
  P->pbuf = potion_asm_clear(P, P->pbuf);
}
# "$#$r", "$#{$r}": last index of the array REF points to
dqlastref = '$#$' n:dqid {
  P->dqpieces = PN_PUSH(P->dqpieces, PN_AST(VALUE, potion_bytes_string(P, PN_NIL, (PN)P->pbuf)));
  P->dqpieces = PN_PUSH(P->dqpieces, PN_OP(AST_MINUS,
      PN_AST(EXPR, PN_PUSH(PN_TUP(PN_AST(MSG, p5_vn(P, "$", n))), PN_AST(MSG, PN_STR("length")))),
      PN_AST(EXPR, PN_TUP(PN_AST(VALUE, PN_NUM(1))))));
  P->pbuf = potion_asm_clear(P, P->pbuf);
}
dqlastref2 = '$#{$' n:dqid '}' {
  P->dqpieces = PN_PUSH(P->dqpieces, PN_AST(VALUE, potion_bytes_string(P, PN_NIL, (PN)P->pbuf)));
  P->dqpieces = PN_PUSH(P->dqpieces, PN_OP(AST_MINUS,
      PN_AST(EXPR, PN_PUSH(PN_TUP(PN_AST(MSG, p5_vn(P, "$", n))), PN_AST(MSG, PN_STR("length")))),
      PN_AST(EXPR, PN_TUP(PN_AST(VALUE, PN_NUM(1))))));
  P->pbuf = potion_asm_clear(P, P->pbuf);
}
# "@a[1,2]", "@h{'a','b'}": the slice joined with a space
dqslice = p:dqslicepair {
  P->dqpieces = PN_PUSH(P->dqpieces, PN_AST(VALUE, potion_bytes_string(P, PN_NIL, (PN)P->pbuf)));
  P->dqpieces = PN_PUSH(P->dqpieces, PN_AST(EXPR, PN_PUSH(PN_TUP(p5_slice(P, G->lineno, P->line, PN_TUPLE_AT(p, 0), PN_TUPLE_AT(p, 1))),
      PN_AST2(MSG, PN_STR("p5joins"), PN_AST(LIST, PN_TUP(p5_listsep(P)))))));
  P->pbuf = potion_asm_clear(P, P->pbuf);
}
dqslicepair = '@' n:dqid '[' l:listexprs - ']' { $$ = PN_PUSH(PN_TUP(p5_vn(P, "@", n)), l) }
            | '@' n:dqid '{' l:listexprs - '}' { $$ = PN_PUSH(PN_TUP(p5_vn(P, "%", n)), l) }
dqarray = '@' n:dqid {
  P->dqpieces = PN_PUSH(P->dqpieces, PN_AST(VALUE, potion_bytes_string(P, PN_NIL, (PN)P->pbuf)));
  P->dqpieces = PN_PUSH(P->dqpieces, PN_AST(EXPR, PN_PUSH(PN_TUP(PN_AST(MSG, p5_vn(P, "@", n))),
      PN_AST2(MSG, PN_STR("p5joins"), PN_AST(LIST, PN_TUP(p5_listsep(P)))))));
  P->pbuf = potion_asm_clear(P, P->pbuf);
}
dqarray2 = '@$' n:dqid {
  P->dqpieces = PN_PUSH(P->dqpieces, PN_AST(VALUE, potion_bytes_string(P, PN_NIL, (PN)P->pbuf)));
  P->dqpieces = PN_PUSH(P->dqpieces, PN_AST(EXPR, PN_PUSH(PN_TUP(PN_AST(MSG, p5_vn(P, "$", n))),
      PN_AST2(MSG, PN_STR("p5joins"), PN_AST(LIST, PN_TUP(p5_listsep(P)))))));
  P->pbuf = potion_asm_clear(P, P->pbuf);
}
# "$+{name}", "$-[0]", "$+[1]": the named group / group offsets of the last match
dqmvar = '$+{' k:dqkeyin '}' {
  P->dqpieces = PN_PUSH(P->dqpieces, PN_AST(VALUE, potion_bytes_string(P, PN_NIL, (PN)P->pbuf)));
  P->dqpieces = PN_PUSH(P->dqpieces, PN_AST2(MSG, PN_STR("%+"), PN_AST(LIST, PN_TUP(k))));
  P->pbuf = potion_asm_clear(P, P->pbuf);
}
       | '$-[' i:dqidx ']' {
  P->dqpieces = PN_PUSH(P->dqpieces, PN_AST(VALUE, potion_bytes_string(P, PN_NIL, (PN)P->pbuf)));
  P->dqpieces = PN_PUSH(P->dqpieces, PN_AST2(MSG, PN_STR("@-"), PN_AST(LIST, PN_TUP(i))));
  P->pbuf = potion_asm_clear(P, P->pbuf);
}
       | '$+[' i:dqidx ']' {
  P->dqpieces = PN_PUSH(P->dqpieces, PN_AST(VALUE, potion_bytes_string(P, PN_NIL, (PN)P->pbuf)));
  P->dqpieces = PN_PUSH(P->dqpieces, PN_AST2(MSG, PN_STR("@+"), PN_AST(LIST, PN_TUP(i))));
  P->pbuf = potion_asm_clear(P, P->pbuf);
}
dqmatch = '$' < ( [1-9] [0-9]* | '&' | '@' | '!' | '?' ) > {
  PN nm = PN_STRN(yytext, yyleng);
  P->dqpieces = PN_PUSH(P->dqpieces, PN_AST(VALUE, potion_bytes_string(P, PN_NIL, (PN)P->pbuf)));
  P->dqpieces = PN_PUSH(P->dqpieces,
    (yytext[0] >= '1' && yytext[0] <= '9')
      ? PN_AST2(MSG, PN_STR("@^M"),
                PN_AST(LIST, PN_TUP(PN_AST(VALUE, PN_NUM(PN_ATOI(yytext, yyleng, 10))))))
      : PN_AST(MSG, p5_vn(P, "$", nm)));
  P->pbuf = potion_asm_clear(P, P->pbuf);
}
dqderef = '$$' < IDFIRST utfw* > {
  PN nm = PN_STRN(yytext, yyleng);
  P->dqpieces = PN_PUSH(P->dqpieces, PN_AST(VALUE, potion_bytes_string(P, PN_NIL, (PN)P->pbuf)));
  P->dqpieces = PN_PUSH(P->dqpieces, p5_deref(P, G->lineno, P->line, PN_AST(MSG, p5_vn(P, "$", nm))));
  P->pbuf = potion_asm_clear(P, P->pbuf);
}
# "@{[ EXPR ]}": the list of EXPR joined by a space (the text up to the first ]} is parsed as code)
dqexpr = '@{[' < ( !']}' . )* > ']}' {
  PN saved = P->dqpieces, lit, code, first = PN_NIL;
  char *buf = (char *)malloc(yyleng + 24);
  int bn;
  if (!buf) potion_allocation_error();
  lit = PN_AST(VALUE, potion_bytes_string(P, PN_NIL, (PN)P->pbuf));
  bn = snprintf(buf, yyleng + 24, "join(' ', %.*s);", (int)yyleng, yytext);
  P->dqpieces = PN_PUSH(P->dqpieces, lit);
  saved = P->dqpieces;
  code = p2_parse(P, PN_STRN(buf, bn), "<@{[]}>");
  free(buf);
  P->dqpieces = saved;
  if (PN_IS_PTR(code) && potion_ptr_type(code) == PN_TSOURCE && PN_IS_TUPLE(PN_S(code, 0)) && PN_TUPLE_LEN(PN_S(code, 0)) > 0)
    first = PN_TUPLE_AT(PN_S(code, 0), 0);
  P->dqpieces = PN_PUSH(P->dqpieces, first != PN_NIL ? first : PN_AST(VALUE, PN_STR("")));
  P->pbuf = potion_asm_clear(P, P->pbuf);
}
# "@{func($x)}": a function call inside the braces (no nested braces)
dqexpr2 = '@{' &(IDFIRST (!'}' !'(' .)* '(') < ( !'}' . )* > '}' {
  PN saved = P->dqpieces, lit, code, first = PN_NIL;
  char *buf = (char *)malloc(yyleng + 24);
  int bn;
  if (!buf) potion_allocation_error();
  lit = PN_AST(VALUE, potion_bytes_string(P, PN_NIL, (PN)P->pbuf));
  bn = snprintf(buf, yyleng + 24, "join(' ', @{%.*s});", (int)yyleng, yytext);
  P->dqpieces = PN_PUSH(P->dqpieces, lit);
  saved = P->dqpieces;
  code = p2_parse(P, PN_STRN(buf, bn), "<@{[]}>");
  free(buf);
  P->dqpieces = saved;
  if (PN_IS_PTR(code) && potion_ptr_type(code) == PN_TSOURCE && PN_IS_TUPLE(PN_S(code, 0)) && PN_TUPLE_LEN(PN_S(code, 0)) > 0)
    first = PN_TUPLE_AT(PN_S(code, 0), 0);
  P->dqpieces = PN_PUSH(P->dqpieces, first != PN_NIL ? first : PN_AST(VALUE, PN_STR("")));
  P->pbuf = potion_asm_clear(P, P->pbuf);
}
# "${$_[0]}" / "${$a[1]}": the scalar a reference element points to
dqderef3 = '${$' n3:dqid '[' - i3:mvalue - ']' '}' {
  P->dqpieces = PN_PUSH(P->dqpieces, PN_AST(VALUE, potion_bytes_string(P, PN_NIL, (PN)P->pbuf)));
  P->dqpieces = PN_PUSH(P->dqpieces, p5_deref(P, G->lineno, P->line, PN_AST2(MSG, p5_vn(P, "@", n3), PN_AST(LIST, PN_TUP(i3)))));
  P->pbuf = potion_asm_clear(P, P->pbuf);
}
dqderef2 = '${$' < IDFIRST utfw* > '}' {
  PN nm = PN_STRN(yytext, yyleng);
  P->dqpieces = PN_PUSH(P->dqpieces, PN_AST(VALUE, potion_bytes_string(P, PN_NIL, (PN)P->pbuf)));
  P->dqpieces = PN_PUSH(P->dqpieces, p5_deref(P, G->lineno, P->line, PN_AST(MSG, p5_vn(P, "$", nm))));
  P->pbuf = potion_asm_clear(P, P->pbuf);
}
# special variables that interpolate: caret names, comma, semicolon (not dot and bracket: a regex may end in them)
dqspecial = '$' < ( '^' [A-Z] | ',' | ';' | '.' | '/' | '$' !'{' | '0' ![0-9] ) > {
  PN nm = PN_STRCAT("$", yytext);
  P->dqpieces = PN_PUSH(P->dqpieces, PN_AST(VALUE, potion_bytes_string(P, PN_NIL, (PN)P->pbuf)));
  P->dqpieces = PN_PUSH(P->dqpieces, PN_AST(MSG, nm));
  P->pbuf = potion_asm_clear(P, P->pbuf);
}
# "${name}": the scalar $name
# "${^NAME}": a special variable with a long name
dqcaret = '${^' < [A-Z] [A-Z0-9_]* > '}' {
  PN nm = PN_STRCAT("$^", yytext);
  if (!strcmp(yytext, "GLOBAL_PHASE")) { p5_end_root(P); p5_phase_used = 1; }
  P->dqpieces = PN_PUSH(P->dqpieces, PN_AST(VALUE, potion_bytes_string(P, PN_NIL, (PN)P->pbuf)));
  P->dqpieces = PN_PUSH(P->dqpieces, PN_AST(MSG, nm));
  P->pbuf = potion_asm_clear(P, P->pbuf);
}
dqbrace = '${' < IDFIRST utfw* ('::' utfw+)* > '}' {
  PN nm = PN_STRN(yytext, yyleng);
  P->dqpieces = PN_PUSH(P->dqpieces, PN_AST(VALUE, potion_bytes_string(P, PN_NIL, (PN)P->pbuf)));
  P->dqpieces = PN_PUSH(P->dqpieces, PN_AST(MSG, p5_vn(P, "$", nm)));
  P->pbuf = potion_asm_clear(P, P->pbuf);
}
dqscalar = '$' < ( '::' IDFIRST utfw* | IDFIRST utfw* ) ('::' utfw+)* > {
  PN nm = PN_STRN(yytext, yyleng);
  P->dqpieces = PN_PUSH(P->dqpieces, PN_AST(VALUE, potion_bytes_string(P, PN_NIL, (PN)P->pbuf)));
  P->dqpieces = PN_PUSH(P->dqpieces, p5_sread(P, G->lineno, P->line, p5_vn(P, "$", nm)));
  P->pbuf = potion_asm_clear(P, P->pbuf);
}
str2 = q2 { P->pbuf = potion_asm_clear(P, P->pbuf); P->dqpieces = PN_TUP0(); }
       < (e2 | escn | escb | escf | escr | esct | escm | escu | escx | escc | dqvar | c2)* >
       q2 { $$ = p5_dq_finish(P, G->lineno); }

# q// and balanced paired-delimiter variants. The capture remains raw so
# p5_q_string can preserve non-delimiter backslashes and suppress interpolation.
q = "q" !utfw - (
      '/' < q-slash-item* > '/'
        { $$ = p5_q_string(P, G->lineno, yytext, yyleng, 0x2f, 0x2f); }
    | '(' < q-paren-item* > ')'
        { $$ = p5_q_string(P, G->lineno, yytext, yyleng, 0x28, 0x29); }
    | '!' < q-bang-item* > '!'
        { $$ = p5_q_string(P, G->lineno, yytext, yyleng, 0x21, 0x21); }
    | '|' < q-bar-item* > '|'
        { $$ = p5_q_string(P, G->lineno, yytext, yyleng, 0x7c, 0x7c); }
    | '[' < q-square-item* > ']'
        { $$ = p5_q_string(P, G->lineno, yytext, yyleng, 0x5b, 0x5d); }
    | '{' < q-brace-item* > '}'
        { $$ = p5_q_string(P, G->lineno, yytext, yyleng, 0x7b, 0x7d); }
    | q-angle-open < q-angle-item* > q-angle-close
        { $$ = p5_q_string(P, G->lineno, yytext, yyleng, 0x3c, 0x3e); }
    | ',' < q-comma-item* > ','
        { $$ = p5_q_string(P, G->lineno, yytext, yyleng, 0x2c, 0x2c); }
    | "'" < q-quote-item* > "'"
        { $$ = p5_q_string(P, G->lineno, yytext, yyleng, 0x27, 0x27); }
    | '"' < q-dquote-item* > '"'
        { $$ = p5_q_string(P, G->lineno, yytext, yyleng, 0x22, 0x22); }
    | '~' < q-tilde-item* > '~'
        { $$ = p5_q_string(P, G->lineno, yytext, yyleng, 0x7e, 0x7e); }
    | '^' < q-caret-item* > '^'
        { $$ = p5_q_string(P, G->lineno, yytext, yyleng, 0x5e, 0x5e); }
    | ';' < q-semi-item* > ';'
        { $$ = p5_q_string(P, G->lineno, yytext, yyleng, 0x3b, 0x3b); }
    | ':' < q-colon-item* > ':'
        { $$ = p5_q_string(P, G->lineno, yytext, yyleng, 0x3a, 0x3a); }
    | '@' < q-at-item* > '@'
        { $$ = p5_q_string(P, G->lineno, yytext, yyleng, 0x40, 0x40); }
    | '+' < q-plus-item* > '+'
        { $$ = p5_q_string(P, G->lineno, yytext, yyleng, 0x2b, 0x2b); }
    | '%' < q-pct-item* > '%'
        { $$ = p5_q_string(P, G->lineno, yytext, yyleng, 0x25, 0x25); }
    | '*' < q-star-item* > '*'
        { $$ = p5_q_string(P, G->lineno, yytext, yyleng, 0x2a, 0x2a); }
    | '#' < q-hash-item* > '#'
        { $$ = p5_q_string(P, G->lineno, yytext, yyleng, 0x23, 0x23); }
    ) -

q-comma-item = esc utf8 | !',' utf8
q-quote-item = esc utf8 | !"'" utf8
q-dquote-item = esc utf8 | !'"' utf8
q-tilde-item = esc utf8 | !'~' utf8
q-caret-item = esc utf8 | !'^' utf8
q-semi-item = esc utf8 | !';' utf8
q-colon-item = esc utf8 | !':' utf8
q-at-item = esc utf8 | !'@' utf8
q-plus-item = esc utf8 | !'+' utf8
q-pct-item = esc utf8 | !'%' utf8
q-star-item = esc utf8 | !'*' utf8
q-hash-item = esc utf8 | !'#' utf8
q-slash-item = esc utf8 | !'/' utf8
q-bang-item = esc utf8 | !'!' utf8
q-bar-item = esc utf8 | !'|' utf8
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
       '!' { p5_dq_start(P); }
         (escn | escb | escf | escr | esct | escm | escu | escx | escc | dqvar | qq-bang-c)*
       '!' { $$ = p5_dq_finish(P, G->lineno); }
     |        '/' { p5_dq_start(P); }
         (escn | escb | escf | escr | esct | escm | escu | escx | escc | dqvar | qq-slash-c)*
       '/' { $$ = p5_dq_finish(P, G->lineno); }
     | '(' { p5_dq_start(P); }
         (escn | escb | escf | escr | esct | escm | escu | escx | escc | dqvar | qq-paren-nested | qq-paren-c)*
       ')' { $$ = p5_dq_finish(P, G->lineno); }
     | '[' { p5_dq_start(P); }
         (escn | escb | escf | escr | esct | escm | escu | escx | escc | dqvar | qq-square-nested | qq-square-c)*
       ']' { $$ = p5_dq_finish(P, G->lineno); }
     | '{' { p5_dq_start(P); }
         (escn | escb | escf | escr | esct | escm | escu | escx | escc | dqvar | qq-brace-nested | qq-brace-c)*
       '}' { $$ = p5_dq_finish(P, G->lineno); }
     | "'" { p5_dq_start(P); }
         (escn | escb | escf | escr | esct | escm | escu | escx | escc | dqvar | qq-quote-c)*
       "'" { $$ = p5_dq_finish(P, G->lineno); }
     | '|' { p5_dq_start(P); }
         (escn | escb | escf | escr | esct | escm | escu | escx | escc | dqvar | qq-bar-c)*
       '|' { $$ = p5_dq_finish(P, G->lineno); }
     | '"' { p5_dq_start(P); }
         (escn | escb | escf | escr | esct | escm | escu | escx | escc | dqvar | qq-dquote-c)*
       '"' { $$ = p5_dq_finish(P, G->lineno); }
     | '~' { p5_dq_start(P); }
         (escn | escb | escf | escr | esct | escm | escu | escx | escc | dqvar | qq-tilde-c)*
       '~' { $$ = p5_dq_finish(P, G->lineno); }
     | '^' { p5_dq_start(P); }
         (escn | escb | escf | escr | esct | escm | escu | escx | escc | dqvar | qq-caret-c)*
       '^' { $$ = p5_dq_finish(P, G->lineno); }
     | '<' { p5_dq_start(P); }
         (escn | escb | escf | escr | esct | escm | escu | escx | escc | dqvar | qq-angle-nested | qq-angle-c)*
       '>' { $$ = p5_dq_finish(P, G->lineno); }
     | &{ p5_qgd_open(G) } xutf8 { p5_dq_start(P); }
         (escn | escb | escf | escr | esct | escm | escu | escx | escc | dqvar | qq-gen-c)*
       qq-gen-close { $$ = p5_dq_finish(P, G->lineno); }
     ) -


qx = "qx" !utfw - (
       '!' { p5_dq_start(P); }
         (escn | escb | escf | escr | esct | escm | escu | escx | escc | dqvar | qq-bang-c)*
       '!' { $$ = p5_readpipe_node(P, p5_dq_finish(P, G->lineno)); }
     |        '/' { p5_dq_start(P); }
         (escn | escb | escf | escr | esct | escm | escu | escx | escc | dqvar | qq-slash-c)*
       '/' { $$ = p5_readpipe_node(P, p5_dq_finish(P, G->lineno)); }
     | '(' { p5_dq_start(P); }
         (escn | escb | escf | escr | esct | escm | escu | escx | escc | dqvar | qq-paren-nested | qq-paren-c)*
       ')' { $$ = p5_readpipe_node(P, p5_dq_finish(P, G->lineno)); }
     | '[' { p5_dq_start(P); }
         (escn | escb | escf | escr | esct | escm | escu | escx | escc | dqvar | qq-square-nested | qq-square-c)*
       ']' { $$ = p5_readpipe_node(P, p5_dq_finish(P, G->lineno)); }
     | '{' { p5_dq_start(P); }
         (escn | escb | escf | escr | esct | escm | escu | escx | escc | dqvar | qq-brace-nested | qq-brace-c)*
       '}' { $$ = p5_readpipe_node(P, p5_dq_finish(P, G->lineno)); }
     ) -


# `command`: readpipe of the interpolated text
bq = &{ p5_qd(0) } '`' { p5_dq_start(P); }
       (escn | escb | escf | escr | esct | escm | escu | escx | escc | '\\`' { P->pbuf = potion_asm_write(P, P->pbuf, "`", 1); } | dqvar | bq-c)*
     '`' { $$ = p5_readpipe_node(P, p5_dq_finish(P, G->lineno)); } -
bq-c = < (!'`' !esc !('$' IDFIRST) !('${$' IDFIRST) utf8)+ >
       { P->pbuf = potion_asm_write(P, P->pbuf, yytext, yyleng); }

qq-quote-c = < (!"'" !esc !('$' IDFIRST) !('${$' IDFIRST) utf8)+ >
             { P->pbuf = potion_asm_write(P, P->pbuf, yytext, yyleng); }
qq-bar-c = < (!'|' !esc !('$' IDFIRST) !('${$' IDFIRST) utf8)+ >
             { P->pbuf = potion_asm_write(P, P->pbuf, yytext, yyleng); }
qq-dquote-c = < (!'"' !esc !('$' IDFIRST) !('${$' IDFIRST) utf8)+ >
             { P->pbuf = potion_asm_write(P, P->pbuf, yytext, yyleng); }
qq-tilde-c = < (!'~' !esc !('$' IDFIRST) !('${$' IDFIRST) utf8)+ >
             { P->pbuf = potion_asm_write(P, P->pbuf, yytext, yyleng); }
qq-caret-c = < (!'^' !esc !('$' IDFIRST) !('${$' IDFIRST) utf8)+ >
             { P->pbuf = potion_asm_write(P, P->pbuf, yytext, yyleng); }
qq-angle-c = < (!'<' !'>' !esc !('$' IDFIRST) !('${$' IDFIRST) utf8)+ >
             { P->pbuf = potion_asm_write(P, P->pbuf, yytext, yyleng); }
qq-angle-nested = '<' { P->pbuf = potion_asm_write(P, P->pbuf, "<", 1); }
                   (escn | escb | escf | escr | esct | escm | escu | escx | escc | dqvar | qq-angle-nested | qq-angle-c)*
                 '>' { P->pbuf = potion_asm_write(P, P->pbuf, ">", 1); }

xutf8 = [\300-\376] [\200-\277]+
qq-gen-close = &{ p5_qgd_at(G) } xutf8
qq-gen-c = < (!qq-gen-close !esc !('$' IDFIRST) !('${$' IDFIRST) (utf8 | xutf8))+ >
             { P->pbuf = potion_asm_write(P, P->pbuf, yytext, yyleng); }
qq-bang-c = < (!'!' !esc !('$' IDFIRST) !('${$' IDFIRST) utf8)+ >
            { P->pbuf = potion_asm_write(P, P->pbuf, yytext, yyleng); }
qq-slash-c = < (!'/' !esc !('$' IDFIRST) !('${$' IDFIRST) utf8)+ >
             { P->pbuf = potion_asm_write(P, P->pbuf, yytext, yyleng); }

qq-paren-c = < (!'(' !')' !esc !('$' IDFIRST) !('${$' IDFIRST) utf8)+ >
             { P->pbuf = potion_asm_write(P, P->pbuf, yytext, yyleng); }
qq-paren-nested = '(' { P->pbuf = potion_asm_write(P, P->pbuf, "(", 1); }
                   (escn | escb | escf | escr | esct | escm | escu | escx | escc | dqvar | qq-paren-nested | qq-paren-c)*
                 ')' { P->pbuf = potion_asm_write(P, P->pbuf, ")", 1); }

qq-square-c = < (!'[' !']' !esc !('$' IDFIRST) !('${$' IDFIRST) utf8)+ >
              { P->pbuf = potion_asm_write(P, P->pbuf, yytext, yyleng); }
qq-square-nested = '[' { P->pbuf = potion_asm_write(P, P->pbuf, "[", 1); }
                    (escn | escb | escf | escr | esct | escm | escu | escx | escc | dqvar | qq-square-nested | qq-square-c)*
                  ']' { P->pbuf = potion_asm_write(P, P->pbuf, "]", 1); }

qq-brace-c = < (!'{' !'}' !esc !('$' IDFIRST) !('${$' IDFIRST) utf8)+ >
             { P->pbuf = potion_asm_write(P, P->pbuf, yytext, yyleng); }
qq-brace-nested = '{' { P->pbuf = potion_asm_write(P, P->pbuf, "\x7b", 1); }
                   (escn | escb | escf | escr | esct | escm | escu | escx | escc | dqvar | qq-brace-nested | qq-brace-c)*
                 '}' { P->pbuf = potion_asm_write(P, P->pbuf, "\x7d", 1); }


# qw(word list) literal, whitespace-separated words between any
# matching delimiter pair; !utfw keeps 'qwx(...)' a normal call.
# (No '<...>' form: '<' '>' collide with greg's capture syntax.)
qw = "qw" !utfw - ( '(' < [^)]* > ')' | '[' < [^\]]* > ']' | '{' < [^}]* > '}' | '<' < [^>]* > '>' | '/' < [^/]* > '/' | '!' < [^!]* > '!' | '|' < [^|]* > '|' | ',' < [^,]* > ',' ) - { $$ = p5_qw_words(P, G->lineno, yytext, yyleng); }
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
dqid = < IDFIRST utfw* ('::' utfw+)* > { $$ = PN_STRN(yytext, yyleng) }
id = < IDFIRST utfw* > { $$ = p5_uni_warn(P, yytext, yyleng) }
# Foo::bar function name: package namespaces are not modelled, it is one flat name
qid = < IDFIRST utfw* ('::' IDFIRST utfw*)* > { $$ = p5_idname(P, G->filename, G->lineno, yytext, yyleng) }
# package-qualified variable name: $::x, $Foo::Bar::x
gid = < '::'? IDFIRST utfw* ('::' utfw+)* > { $$ = p5_idname(P, G->filename, G->lineno, yytext, yyleng) }
# isWORDCHAR && IDFIRST, no numbers
IDFIRST = [A-Za-z_]
     | ( [\300-\337] [\200-\277] | [\340-\357] [\200-\277] [\200-\277] | [\360-\364] [\200-\277] [\200-\277] [\200-\277] ) &{ yy_uni(G, 1) }
# isWORDCHAR? \w and [:word:]
utfw = [A-Za-z0-9_]
     | ( [\300-\337] [\200-\277] | [\340-\357] [\200-\277] [\200-\277] | [\360-\364] [\200-\277] [\200-\277] [\200-\277] ) &{ yy_uni(G, 0) }
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
     | n:arg2-name '?' -				{ SRC_TPL3(n,PN_NUM(':'), PN_NIL) }
     | n:arg2-name - assign d:value			{ SRC_TPL3(n,PN_NUM(':'), PN_S(d,0)) }
     | n:arg2-name					{ SRC_TPL1(n) }


%%

/* a name with non-ASCII characters: the TR39 checks (mixed scripts, confusables) only warn */
static PN p5_uni_warn_at(Potion *P, const char *file, long line, const char *name, int len) {
  int i;
  for (i = 0; i < len; i++)
    if ((unsigned char)name[i] >= 0x80) {
      const char *msg = potion_uni_ident_check(name, len);
      if (msg) fprintf(stderr, "%s:%ld: Warning: %s: %.*s\n", file ? file : "-", line, msg, len, name);
      break;
    }
  return PN_STRN((char *)name, len);
}
/* The user operators of p5 macros are matched character by character, so that the rules consume input
 * (a rule that can match the empty string made `power` "possibly left recursive") and so that the input
 * buffer is refilled as needed: mark() remembers the start, step() accepts a next character that keeps
 * the text a prefix of an operator of KIND, done() says the text is exactly an operator (a word operator must
 * not continue as an identifier). */
static int p5_mop_start;
static int p5_mop_mark(GREG *G) { p5_mop_start = G->pos; return 1; }
static int p5_mop_step(GREG *G, int kind) {
  const char *t = G->buf + p5_mop_start;
  int n = G->pos - p5_mop_start + 1, i;
  for (i = 0; i < p5_mops_n; i++)
    if (p5_mops[i].kind == kind && (int)strlen(p5_mops[i].text) >= n && !strncmp(p5_mops[i].text, t, n)) return 1;
  return 0;
}
static int p5_mop_done(GREG *G, int kind) {
  const char *t = G->buf + p5_mop_start;
  int n = G->pos - p5_mop_start, i;
  for (i = 0; i < p5_mops_n; i++) {
    if (p5_mops[i].kind != kind || (int)strlen(p5_mops[i].text) != n || strncmp(p5_mops[i].text, t, n)) continue;
    if ((isalpha((unsigned char)p5_mops[i].text[0]) || p5_mops[i].text[0] == '_') && G->pos < G->limit &&
        (isalnum((unsigned char)G->buf[G->pos]) || G->buf[G->pos] == '_')) continue;
    return 1;
  }
  return 0;
}
/* the UTF-8 character just matched ends at the input position: is it an XID_Start/XID_Continue
 * identifier character (core/unicode.c)? Looks back, never refills the input buffer. */
static int yy_uni(GREG *G, int first) {
  long q = G->pos, n;
  while (q > 0 && G->pos - q < 4 && ((unsigned char)G->buf[q - 1] & 0xC0) == 0x80) q--;
  if (q <= 0 || (unsigned char)G->buf[q - 1] < 0xC0) return 0;
  q--;
  n = G->pos - q;
  return potion_uni_ident_len(G->buf + q, n, first) == n;
}

/* The patterns of the tree predicates are built on their first use, and building allocates:
 * do it before the parse starts, so that no predicate triggers a gc while a caller holds a raw
 * PN_STR_PTR (a gc moves strings) */
static void p5_init_patterns(Potion *P) {
  static int done;
  if (done) return;
  done = 1;
  p5_undef_target(P, PN_NIL); p5_unparen(P, PN_NIL); p5_cond(P, 0, PN_NIL, PN_NIL);
  p5_arrayish(P, PN_NIL); p5_is_elem(P, PN_NIL); p5_is_cat(P, PN_NIL); p5_is_listlit(P, PN_NIL);
  p5_is_unq(P, PN_NIL); p5_is_hints_read(P, PN_NIL); p5_is_subdef(P, PN_NIL);
  p5_is_field_ref(P, PN_NIL, PN_NIL); p5_slurpy_sigil(P, PN_NIL); p5_list_call(P, PN_NIL, 0);
  p5_top_args(P, PN_NIL);
}

/* eval "STRING" with a syntax error longjmps out of the parse: free the greg state */
static void p5_parse_abort(void *G) {
  p5_parse_depth--;
  YY_NAME(parse_free)((GREG *)G);
}

/* does the source use the pos function: the word pos (not a method, hash key or longer name) */
static int p5_mentions_pos(const char *c) {
  const char *p = c;
  while ((p = strstr(p, "pos"))) {
    int before = p == c ? ' ' : (unsigned char)p[-1], after = (unsigned char)p[3];
    int wb = before == '_' || before == '$' || before == '@' || before == '%' || before == '>' || before == ':' || before == '&' || (before >= '0' && before <= '9') ||
             (before >= 'a' && before <= 'z') || (before >= 'A' && before <= 'Z');
    int wa = after == '_' || (after >= '0' && after <= '9') || (after >= 'a' && after <= 'z') || (after >= 'A' && after <= 'Z');
    if (!wb && !wa) return 1;
    p += 3;
  }
  return 0;
}
PN p2_parse(Potion *P, PN code, char *filename) {
  GREG *G = YY_NAME(parse_new)(P);
  p5_init_patterns(P);
  int oldyypos = P->yypos;
  PN oldinput = P->input;
  PN oldsource = P->source;
  struct PNParseRoot root;
  code = p5_expand_heredocs(P, code);
  P->yypos = 0;
  P->input = code;
  P->source = PN_NIL;
  P->pbuf = potion_asm_new(P);
  p5_prescan_subs(PN_STR_PTR(code), PN_STR_LEN(code));
  /* a required file or eval string parses while the actions of the outer parse run: the flags of the outer source come back after it */
  int old_ofs = p5_ofs_used, old_want = p5_want_used, old_pos = p5_pos_used, old_lsep = p5_lsep_used;
  int old_repl = p5_repl, old_in_my = p5_in_my, old_rdecl_n = p5_rdecl_n;
  p5_repl = 0; p5_in_my = 0;
  if (filename && !strcmp(filename, "<eval>")) { /* the loop of p2 -d sets $P2::repl: see p5_vn */
    PN fk = potion_str(P, "$P2::repl"), rv = potion_bind(P, P->lobby, fk) == PN_NIL ? PN_NIL : potion_send(P->lobby, fk);
    p5_repl = PN_IS_INT(rv) && PN_INT(rv) != 0;
  } else if (filename && filename[0] == '<' && p5_parse_depth > 0) {
    p5_repl = old_repl; /* "<@{[]}>", "<s///>": the expression of a string or a replacement, inside the input being parsed */
  }
  p5_ofs_used = strstr(PN_STR_PTR(code), "$,") != NULL;
  p5_want_used = strstr(PN_STR_PTR(code), "wantarray") != NULL;
  p5_pos_used = p5_mentions_pos(PN_STR_PTR(code));
  p5_lsep_used = strstr(PN_STR_PTR(code), "$\"") != NULL;
  p5_prescan_macros(PN_STR_PTR(code), PN_STR_LEN(code));
#ifdef DEBUG
  yydebug = P->flags;
#endif

  G->filename = filename;
  root.ss = &G->ss; root.vals = &G->vals; root.nvals = &G->valslen;
  root.prev = P->parse_roots;
  root.abort = p5_parse_abort; root.parse = G;
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
  p5_ofs_used = old_ofs; p5_want_used = old_want; p5_pos_used = old_pos; p5_lsep_used = old_lsep;
  p5_repl = old_repl; p5_in_my = old_in_my; p5_rdecl_n = old_rdecl_n;

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
  if (PN_TYPE(cl) == PN_TVTABLE) /* calling a class: its ctor holds the sig */
    cl = ((struct PNVtable *)cl)->ctor;
  else if (!PN_IS_CLOSURE(cl))
    cl = potion_obj_get_call(P, cl);

  if (!PN_IS_CLOSURE(cl))
    return -1;

  sig = PN_CLOSURE(cl)->sig;
  if (!PN_IS_TUPLE(sig))
    return -1;

  /* sig entries: name, name type, name ':' default, or name 0 modifier.
   * Names from other front ends (p6) are not interned: compare content. */
  {
    PN_SIZE i, n = PN_TUPLE_LEN(sig);
    for (i = 0; i < n; i++) {
      PN v = PN_TUPLE_AT(sig, i);
      if (!PN_IS_STR(v)) continue;
      if (v == name ||
          (PN_IS_STR(name) && PN_STR_LEN(v) == PN_STR_LEN(name)
           && !memcmp(PN_STR_PTR(v), PN_STR_PTR(name), PN_STR_LEN(name))))
        return idx;
      idx++;
      if (i + 2 < n && PN_TUPLE_AT(sig, i + 1) == PN_NUM(':'))
        i += 2; /* skip the default value, it may be a string */
    }
  }

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

/* qq with any non-ASCII delimiter (perl takes one (extended) UTF-8 character): the opening one is recorded by a
 * predicate, the closing one is the same byte sequence */
static char p5_qgd[8];
static int p5_qgdn;
static int p5_qgd_open(struct _GREG *G) {
  int n = 1;
  unsigned char c;
  if (G->pos >= G->limit) return 0;
  c = (unsigned char)G->buf[G->pos];
  if (c < 0xc0) return 0;
  while (n < 7 && G->pos + n < G->limit && ((unsigned char)G->buf[G->pos + n] & 0xc0) == 0x80) n++;
  memcpy(p5_qgd, G->buf + G->pos, n); p5_qgdn = n;
  return 1;
}
static int p5_qgd_at(struct _GREG *G) {
  return p5_qgdn && G->pos + p5_qgdn <= G->limit && !memcmp(G->buf + G->pos, p5_qgd, p5_qgdn);
}


/* does the text of <...> look like a file glob (a wildcard, a path or a brace) and not a handle name */
static int p5_globchars(const char *s) { return strpbrk(s, "*?[/.~{") != NULL; }
