/* pvip_to_pn.c -- translate PVIPNode* (pvip/p6 AST) to PNSource (p2 AST)
 *
 * 70% of PVIP nodes map directly to existing PNSource AST nodes.
 * The remaining 30% (junctions, roles, regex, etc.) emit p6_*() runtime
 * calls that are resolved against libp6 at runtime.
 *
 * Exports: PN syntax_parse(Potion *P, PN src, const char *filename)
 *
 * (c) 2014 by perl11 org */

#ifndef P2
# define P2
#endif
#include "p2.h"
#include "internal.h"
#include "ast.h"
#include "pvip.h"
#include <string.h>
#include <stdio.h>

#define DBG_Pv(p) \
    if (P->flags & DEBUG_VERBOSE) \
      potion_p(P, p)
#define DBG_Pvi(p) \
    if (P->flags & (DEBUG_INSPECT|DEBUG_VERBOSE)) \
      potion_p(P, p)

/* local helpers — no G context here, so line text is PN_NIL */
#define LN  (node->line_number)
#define SRC(T,A)        potion_source(P, AST_##T, (A),    PN_NIL, PN_NIL, LN, PN_NIL)
#define SRC2(T,A,B)     potion_source(P, AST_##T, (A),(B),PN_NIL, LN, PN_NIL)
#define SRC3(T,A,B,C)   potion_source(P, AST_##T, (A),(B),(C),    LN, PN_NIL)
#define NC              (node->children.size)
#define CHILD(i)        pvip_to_pn(P, node->children.nodes[i])
#define EXPR(x)         SRC(EXPR, PN_TUP(x))
#define MSG(n,a)        SRC2(MSG, (n), (a))
#define CALL(n,a)       EXPR(MSG((n),(a)))
#define LIST(t)         SRC(LIST, (t))
#define TUP2(a,b)       ({ PN _t = PN_TUP(a); PN_PUSH(_t, b); _t; })

static PN pvip_to_pn(Potion *P, PVIPNode *node);

/* p5/p2 locals are named with their sigil included (".local $x" vs "@x" are
 * distinct slots, see syntax-p5.y's scalar/listvar/funcvar rules) -- keep it,
 * don't strip it, or "my @a" and "my $a" alias the same local and corrupt
 * each other (observed as a got-a-Tuple-instead-of-a-Num infinite recursion
 * in potion_any_cmp). Name kept for caller-compat. */
static PN strip_sigil(Potion *P, PVIPNode *node) {
  return PN_STRN(node->pv->buf, node->pv->len);
}

/* emit a p6 runtime call: p6_name(children...) */
static PN p6_call(Potion *P, PVIPNode *node, const char *name) {
  PN args = PN_TUP0();
  int i;
  for (i = 0; i < NC; i++) PN_PUSH(args, CHILD(i));
  return CALL(PN_STRN((char*)name, strlen(name)), LIST(args));
}

/* emit a p6 runtime call: p6_name(text) -- for nodes built via
 * PVIP_node_new_string() (->pv set, no ->children array). Routing these
 * through p6_call() reads node->children.{size,nodes} off the same union
 * storage as ->pv and crashes on garbage; this reads the string instead. */
static PN p6_call_str(Potion *P, PVIPNode *node, const char *name) {
  PN arg = node->pv ? PN_STRN(node->pv->buf, node->pv->len) : PN_STRN("", 0);
  return CALL(PN_STRN((char*)name, strlen(name)), LIST(PN_TUP(SRC(VALUE, arg))));
}

/* translate a maybe-absent params node (grammar uses MAYBE(p), which
 * yields a NOP node rather than an empty PVIP_NODE_PARAMS when a sub/
 * method has '()' or no parens at all) into a proper PROTO sig LIST.
 * Feeding a NOP's translated shape straight to potion_sig_compile
 * corrupts/crashes it (PN_TUPLE_EACH over a non-tuple). */
static PN pvip_params(Potion *P, PVIPNode *node) {
  if (!node || node->type != PVIP_NODE_PARAMS)
    return potion_source(P, AST_LIST, PN_NIL, PN_NIL, PN_NIL, 1, PN_NIL);
  return pvip_to_pn(P, node);
}

/* Attribute signature of every class translated so far, in `has` order. The VM
 * fills default arguments from the first unsupplied slot on, so a named
 * argument (`Foo.new(y => 4)`) that skips an earlier attribute would leave that
 * slot unset. Foo.new(...) therefore passes its pairs positionally whenever the
 * class is known at translate time. */
#define P6_MAXATTR 32
typedef struct {
  char name[48];
  int  kind;       /* 0 nil, 1 int, 2 string */
  long iv;
  char sv[64];
} p6_attr_t;
typedef struct { char name[64]; int n; p6_attr_t a[P6_MAXATTR]; } p6_class_t;
#define P6_MAXCLASS 256
static p6_class_t p6_classes[P6_MAXCLASS];
static int p6_nclasses;

static p6_class_t *p6_class_find(const char *name, size_t len) {
  int i;
  for (i = 0; i < p6_nclasses; i++)
    if (strlen(p6_classes[i].name) == len && !memcmp(p6_classes[i].name, name, len))
      return &p6_classes[i];
  return NULL;
}

/* (re)start the record of class NAME */
static p6_class_t *p6_class_begin(const char *name, size_t len) {
  p6_class_t *c = p6_class_find(name, len);
  if (!c) {
    if (p6_nclasses == P6_MAXCLASS || len >= sizeof(c->name)) return NULL;
    c = &p6_classes[p6_nclasses++];
    memcpy(c->name, name, len); c->name[len] = 0;
  }
  c->n = 0;
  return c;
}

/* `$.x` / `$!x` / `@.x` -> ivar name "x" (sigil and twigil dropped);
 * a plain `$x` keeps its name. */
static PN ivar_name(Potion *P, PVIPNode *v) {
  if (v->type == PVIP_NODE_ATTRIBUTE_VARIABLE && v->pv->len > 2)
    return PN_STRN(v->pv->buf + 2, v->pv->len - 2);
  return PN_STRN(v->pv->buf, v->pv->len);
}

/* obj.method(args): a message send is the EXPR tuple (obj, MSG(method[, args])),
 * same shape as syntax.y's `obj method (args)`. */
static PN pvip_send(Potion *P, PVIPNode *node, PN obj, PN method, PN args) {
  PN msg = (args != PN_NIL && PN_TUPLE_LEN(args) > 0)
             ? potion_source(P, AST_MSG, method, LIST(args), PN_NIL, LN, PN_NIL)
             : potion_source(P, AST_MSG, method, PN_NIL, PN_NIL, LN, PN_NIL);
  if (PN_PART(obj) == AST_EXPR) {
    PN t = PN_S(obj, 0);
    PN_PUSH(t, msg);
    return obj;
  }
  return SRC(EXPR, TUP2(obj, msg));
}

/* a variable or constant: safe to evaluate twice */
static int p6_simple_operand(PN e) {
  PN st, m;
  if (PN_PART(e) == AST_VALUE) return 1;
  if (PN_PART(e) != AST_EXPR) return 0;
  st = PN_S(e, 0);
  if (!PN_IS_TUPLE(st) || PN_TUPLE_LEN(st) != 1) return 0;
  m = PN_TUPLE_AT(st, 0);
  if (PN_PART(m) == AST_VALUE) return 1;
  return PN_PART(m) == AST_MSG && PN_S(m, 1) == PN_NIL && PN_IS_STR(PN_S(m, 0));
}
/* a == b: the native EQ compares bit patterns, wrong for "4" == 4 and for two
 * equal doubles. For simple operands retry numerically (p5numeq, libp5). */
static PN p6_numeq(Potion *P, PVIPNode *node, PN a, PN b) {
  PN eq = SRC2(EQ, a, b);
  if (!p6_simple_operand(a) || !p6_simple_operand(b)) return eq;
  return SRC2(OR, eq, CALL(PN_STRN("p5numeq", 7), LIST(TUP2(a, b))));
}

static PN pvip_to_pn(Potion *P, PVIPNode *node) {
  if (!node) return PN_NIL;

  switch (node->type) {

  /* --- literals --- */
  case PVIP_NODE_INT:    return SRC(VALUE, PN_NUM((int)node->iv));
  case PVIP_NODE_NUMBER: return SRC(VALUE, potion_double(P, node->nv));
  case PVIP_NODE_STRING: return SRC(VALUE, PN_STRN(node->pv->buf, node->pv->len));
  case PVIP_NODE_TRUE:   return SRC(VALUE, PN_TRUE);
  case PVIP_NODE_FALSE:  return SRC(VALUE, PN_FALSE);
  case PVIP_NODE_UNDEF:  return SRC(VALUE, PN_NIL);
  case PVIP_NODE_PI:     return p6_call(P, node, "p6_pi");
  case PVIP_NODE_E:      return p6_call(P, node, "p6_e");

  /* --- statement containers --- */
  case PVIP_NODE_STATEMENTS: {
    PN stmts = PN_TUP0();
    int i;
    for (i = 0; i < NC; i++) {
      PVIPNode *st = node->children.nodes[i];
      PVIPNode *v = (st->type == PVIP_NODE_MY && st->children.size >= 2)
                    ? st->children.nodes[1] : NULL;
      if (v && v->type == PVIP_NODE_VARIABLE && v->pv && v->pv->len > 1
          && (v->pv->buf[0] == '@' || v->pv->buf[0] == '%')) {
        /* a bare `my @a;` / `my %h;` is an empty array / hash, not undef */
        PN init = v->pv->buf[0] == '@' ? LIST(PN_NIL)
                  : EXPR(SRC(MSG, PN_STRN("table", 5)));
        if (v->pv->buf[0] == '%')
          init = SRC(EXPR, PN_PUSH(PN_TUP(LIST(PN_NIL)), MSG(PN_STRN("table", 5), PN_NIL)));
        PN_PUSH(stmts, SRC2(ASSIGN, EXPR(MSG(strip_sigil(P, v), PN_NIL)), init));
      } else
        PN_PUSH(stmts, pvip_to_pn(P, st));
    }
    return SRC(CODE, stmts);
  }
  case PVIP_NODE_BLOCK: {
    PN stmts = PN_TUP0();
    int i;
    for (i = 0; i < NC; i++) PN_PUSH(stmts, CHILD(i));
    return SRC(BLOCK, stmts);
  }
  case PVIP_NODE_LIST: {
    PN items = PN_TUP0();
    int i;
    for (i = 0; i < NC; i++) PN_PUSH(items, CHILD(i));
    return LIST(items);
  }
  case PVIP_NODE_ARRAY: {
    PN elems = PN_TUP0();
    int i;
    for (i = 0; i < NC; i++) PN_PUSH(elems, CHILD(i));
    return LIST(elems);
  }
  case PVIP_NODE_HASH: {
    PN pairs = PN_TUP0();
    int i;
    for (i = 0; i < NC; i++) PN_PUSH(pairs, CHILD(i));
    return SRC(LICK, pairs);
  }
  case PVIP_NODE_NOP: return SRC(MSG, PN_STRN("nop", 3));

  /* --- arithmetic (direct AST mapping) --- */
  case PVIP_NODE_ADD: return SRC2(PLUS,  CHILD(0), CHILD(1));
  case PVIP_NODE_SUB: return SRC2(MINUS, CHILD(0), CHILD(1));
  case PVIP_NODE_MUL: return SRC2(TIMES, CHILD(0), CHILD(1));
  case PVIP_NODE_DIV: return SRC2(DIV,   CHILD(0), CHILD(1));
  case PVIP_NODE_MOD: return SRC2(REM,   CHILD(0), CHILD(1));
  case PVIP_NODE_POW: return SRC2(POW,   CHILD(0), CHILD(1));

  case PVIP_NODE_UNARY_PLUS:  return SRC2(PLUS, CHILD(0), SRC(VALUE, PN_NUM(0))); /* numify */
  case PVIP_NODE_UNARY_MINUS: return SRC2(MINUS, SRC(VALUE, PN_NUM(0)), CHILD(0));

  /* --- comparisons --- */
  case PVIP_NODE_EQ:  return p6_numeq(P, node, CHILD(0), CHILD(1));
  case PVIP_NODE_NE:  return SRC(NOT, p6_numeq(P, node, CHILD(0), CHILD(1)));
  case PVIP_NODE_LT:  return SRC2(LT,  CHILD(0), CHILD(1));
  case PVIP_NODE_LE:  return SRC2(LTE, CHILD(0), CHILD(1));
  case PVIP_NODE_GT:  return SRC2(GT,  CHILD(0), CHILD(1));
  case PVIP_NODE_GE:  return SRC2(GTE, CHILD(0), CHILD(1));
  case PVIP_NODE_CMP:
  case PVIP_NODE_NUM_CMP: return SRC2(CMP, CHILD(0), CHILD(1));

  /* --- logic --- */
  case PVIP_NODE_LOGICAL_AND: return SRC2(AND, CHILD(0), CHILD(1));
  case PVIP_NODE_LOGICAL_OR:  return SRC2(OR,  CHILD(0), CHILD(1));
  case PVIP_NODE_NOT:         return SRC(NOT, CHILD(0));

  /* --- bitwise --- */
  case PVIP_NODE_BIN_AND:               return SRC2(AMP,    CHILD(0), CHILD(1));
  case PVIP_NODE_BIN_OR:                return SRC2(PIPE,   CHILD(0), CHILD(1));
  case PVIP_NODE_BIN_XOR:               return SRC2(CARET,  CHILD(0), CHILD(1));
  case PVIP_NODE_BLSHIFT:               return SRC2(BITL,   CHILD(0), CHILD(1));
  case PVIP_NODE_BRSHIFT:               return SRC2(BITR,   CHILD(0), CHILD(1));
  case PVIP_NODE_UNARY_BITWISE_NEGATION:return SRC(WAVY, CHILD(0));

  /* --- inplace ops (expand to assign + op) --- */
  case PVIP_NODE_INPLACE_ADD:    return SRC2(ASSIGN,CHILD(0),SRC2(PLUS, CHILD(0),CHILD(1)));
  case PVIP_NODE_INPLACE_SUB:    return SRC2(ASSIGN,CHILD(0),SRC2(MINUS,CHILD(0),CHILD(1)));
  case PVIP_NODE_INPLACE_MUL:    return SRC2(ASSIGN,CHILD(0),SRC2(TIMES,CHILD(0),CHILD(1)));
  case PVIP_NODE_INPLACE_DIV:    return SRC2(ASSIGN,CHILD(0),SRC2(DIV,  CHILD(0),CHILD(1)));
  case PVIP_NODE_INPLACE_POW:    return SRC2(ASSIGN,CHILD(0),SRC2(POW,  CHILD(0),CHILD(1)));
  case PVIP_NODE_INPLACE_MOD:    return SRC2(ASSIGN,CHILD(0),SRC2(REM,  CHILD(0),CHILD(1)));
  case PVIP_NODE_INPLACE_BIN_OR: return SRC2(ASSIGN,CHILD(0),SRC2(PIPE, CHILD(0),CHILD(1)));
  case PVIP_NODE_INPLACE_BIN_AND:return SRC2(ASSIGN,CHILD(0),SRC2(AMP,  CHILD(0),CHILD(1)));
  case PVIP_NODE_INPLACE_BIN_XOR:return SRC2(ASSIGN,CHILD(0),SRC2(CARET,CHILD(0),CHILD(1)));
  case PVIP_NODE_INPLACE_BLSHIFT:return SRC2(ASSIGN,CHILD(0),SRC2(BITL, CHILD(0),CHILD(1)));
  case PVIP_NODE_INPLACE_BRSHIFT:return SRC2(ASSIGN,CHILD(0),SRC2(BITR, CHILD(0),CHILD(1)));

  /* --- variables --- */
  case PVIP_NODE_IDENT:
  case PVIP_NODE_VARIABLE:
    return EXPR(MSG(strip_sigil(P, node), PN_NIL));

  case PVIP_NODE_MY: {
    /* grammar: PVIP_node_new_children2(MY, MAYBE(type), variable)
     * children[0] = optional type (NOP if absent), children[1] = variable
     * For list form "my ($a,$b)": children[0] = variable list (NC==1) */
    PVIPNode *var_node = (NC >= 2) ? node->children.nodes[1]
                                   : node->children.nodes[0];
    if (var_node->type == PVIP_NODE_LIST) {
      /* my ($a, $b, ...): emit an AST_LIST of declared-var exprs; the
       * enclosing LIST_ASSIGNMENT (if any) destructures it positionally. */
      PN items = PN_TUP0();
      int i;
      for (i = 0; i < var_node->children.size; i++)
        PN_PUSH(items, EXPR(MSG(strip_sigil(P, var_node->children.nodes[i]), PN_NIL)));
      return LIST(items);
    }
    PN name = strip_sigil(P, var_node);
    return EXPR(MSG(name, PN_NIL));
  }
  case PVIP_NODE_LIST_ASSIGNMENT: {
    /* destructuring: my ($a,$b,...) = EXPR  or  ($a,$b,...) = EXPR
     * No multi-target assign in the underlying VM: stash EXPR in a temp
     * and bind each var positionally via p6_atpos. */
    PVIPNode *lhs = node->children.nodes[0];
    PVIPNode *vars = NULL;
    if (lhs->type == PVIP_NODE_MY)
      vars = (lhs->children.size >= 2) ? lhs->children.nodes[1] : lhs->children.nodes[0];
    else if (lhs->type == PVIP_NODE_LIST)
      vars = lhs;
    if (vars && vars->type == PVIP_NODE_LIST) {
      static int tmpctr = 0;
      char tmpname[32];
      int n = snprintf(tmpname, sizeof(tmpname), "__p6_destr_%d", tmpctr++);
      PN tmp = PN_STRN(tmpname, n);
      PN stmts = PN_TUP(SRC2(ASSIGN, EXPR(MSG(tmp, PN_NIL)), CHILD(1)));
      int i;
      for (i = 0; i < vars->children.size; i++) {
        PN nm = strip_sigil(P, vars->children.nodes[i]);
        PN idxcall = CALL(PN_STRN("p6_atpos", 8),
                           LIST(TUP2(EXPR(MSG(tmp, PN_NIL)), SRC(VALUE, PN_NUM(i)))));
        PN_PUSH(stmts, SRC2(ASSIGN, EXPR(MSG(nm, PN_NIL)), idxcall));
      }
      return SRC(CODE, stmts);
    }
    if (lhs->type == PVIP_NODE_ATPOS && lhs->children.size == 2) {
      /* @a[i] = v: put grows the array up to i */
      PN args = PN_TUP(pvip_to_pn(P, lhs->children.nodes[1]));
      PN_PUSH(args, CHILD(1));
      return pvip_send(P, node, pvip_to_pn(P, lhs->children.nodes[0]), PN_STRN("put", 3), args);
    }
    return SRC2(ASSIGN, CHILD(0), CHILD(1));
  }

  /* --- inc/dec --- */
  case PVIP_NODE_POSTINC:
  case PVIP_NODE_PREINC:  return SRC(INC, CHILD(0));
  case PVIP_NODE_POSTDEC:
  case PVIP_NODE_PREDEC:
    return SRC2(ASSIGN, CHILD(0), SRC2(MINUS, CHILD(0), SRC(VALUE, PN_NUM(1))));

  /* --- functions / closures --- */
  case PVIP_NODE_PARAMS: {
    PN params = PN_TUP0();
    int i;
    for (i = 0; i < NC; i++) PN_PUSH(params, CHILD(i));
    return LIST(params);
  }
  case PVIP_NODE_ARGS: {
    PN args = PN_TUP0();
    int i;
    for (i = 0; i < NC; i++) PN_PUSH(args, CHILD(i));
    return LIST(args);
  }
  case PVIP_NODE_FUNC: {
    /* grammar: children4(FUNC, name, params, return_type_or_NOP, body) */
    PN name   = PN_STRN(node->children.nodes[0]->pv->buf,
                        node->children.nodes[0]->pv->len);
    PN params = pvip_params(P, node->children.nodes[1]);
    PN body   = CHILD(3);
    return SRC2(ASSIGN, EXPR(MSG(name, PN_NIL)), EXPR(SRC2(PROTO, params, body)));
  }
  case PVIP_NODE_LAMBDA: {
    PN params = NC > 1 ? pvip_params(P, node->children.nodes[0]) : LIST(PN_NIL);
    PN body   = NC > 1 ? CHILD(1) : CHILD(0);
    return SRC2(PROTO, params, body);
  }
  case PVIP_NODE_FUNCALL: {
    PVIPNode *fn = node->children.nodes[0];
    /* children[1] is an ARGS/LIST node; flatten it so we don't emit
     * a nested list(list(...)) */
    PVIPNode *argn = NC > 1 ? node->children.nodes[1] : NULL;
    int argc = argn ? argn->children.size : 0;
    PN fname = PN_STRN(fn->pv->buf, fn->pv->len);
    int i;
    /* one argument math builtins are messages on the number (libp5): sqrt(x) */
    if (argc == 1 && fn->pv->len <= 4) {
      static const char *math[] = { "sqrt", "sin", "cos", "exp", "log", "int", "hex", "oct", 0 };
      for (i = 0; math[i]; i++)
        if (strlen(math[i]) == fn->pv->len && !memcmp(math[i], fn->pv->buf, fn->pv->len))
          return pvip_send(P, node, pvip_to_pn(P, argn->children.nodes[0]), fname, PN_NIL);
    }
    /* IO builtins are messages on the value, as in the p5 grammar:
     * say "x"  =>  expr ("x", msg("say")) */
    if ((fn->pv->len == 3 && !memcmp(fn->pv->buf, "say", 3)) ||
        (fn->pv->len == 5 && !memcmp(fn->pv->buf, "print", 5))) {
      int is_say = (fn->pv->len == 3);
      if (argc == 0) {
        if (!is_say) return PN_NIL;     /* print() with no args: nop */
        PN t = PN_TUP(SRC(VALUE, PN_STRN("", 0)));
        PN_PUSH(t, MSG(fname, PN_NIL)); /* say(): just the newline */
        return SRC(EXPR, t);
      }
      if (argc == 1 && is_say) {
        PN t = PN_TUP(pvip_to_pn(P, argn->children.nodes[0]));
        PN_PUSH(t, MSG(fname, PN_NIL));
        return SRC(EXPR, t);
      }
      /* multiple args (or print): print each, say appends a newline */
      {
        PN stmts = PN_TUP0();
        for (i = 0; i < argc; i++) {
          PN t = PN_TUP(pvip_to_pn(P, argn->children.nodes[i]));
          PN_PUSH(t, MSG(PN_STRN("print", 5), PN_NIL));
          PN_PUSH(stmts, SRC(EXPR, t));
        }
        if (is_say) {
          PN t = PN_TUP(SRC(VALUE, PN_STRN("", 0)));
          PN_PUSH(t, MSG(PN_STRN("say", 3), PN_NIL));
          PN_PUSH(stmts, SRC(EXPR, t));
        }
        return SRC(CODE, stmts);
      }
    }
    {
      PN args = PN_TUP0();
      for (i = 0; i < argc; i++)
        PN_PUSH(args, pvip_to_pn(P, argn->children.nodes[i]));
      return CALL(fname, LIST(args));
    }
  }
  case PVIP_NODE_METHODCALL: {
    PVIPNode *mn = node->children.nodes[1];
    if (mn->pv && mn->pv->len == 3 && !memcmp(mn->pv->buf, "new", 3)
        && node->children.nodes[0]->type == PVIP_NODE_IDENT) {
      /* Foo.new(args) == Foo(args): calling a class object instantiates it */
      PVIPNode *cn = node->children.nodes[0];
      p6_class_t *rec = p6_class_find(cn->pv->buf, cn->pv->len);
      PN args = PN_TUP0();
      if (NC > 2) {
        PVIPNode *argn = node->children.nodes[2];
        int i, k, haspair = 0;
        for (i = 0; i < (int)argn->children.size; i++)
          if (argn->children.nodes[i]->type == PVIP_NODE_PAIR) haspair = 1;
        if (haspair && rec) {
          /* named -> positional, in attribute order; unnamed slots get the
           * declared default. Trailing unsupplied attributes are left off. */
          int last = -1;
          PVIPNode *val[P6_MAXATTR] = {0};
          for (i = 0; i < (int)argn->children.size; i++) {
            PVIPNode *a = argn->children.nodes[i];
            if (a->type != PVIP_NODE_PAIR || a->children.size != 2
                || !a->children.nodes[0]->pv) goto named_fallback;
            PVIPNode *key = a->children.nodes[0];
            for (k = 0; k < rec->n; k++)
              if (strlen(rec->a[k].name) == key->pv->len
                  && !memcmp(rec->a[k].name, key->pv->buf, key->pv->len)) break;
            if (k == rec->n) goto named_fallback;
            val[k] = a->children.nodes[1];
            if (k > last) last = k;
          }
          for (k = 0; k <= last; k++) {
            if (val[k]) PN_PUSH(args, pvip_to_pn(P, val[k]));
            else if (rec->a[k].kind == 1) PN_PUSH(args, SRC(VALUE, PN_NUM((int)rec->a[k].iv)));
            else if (rec->a[k].kind == 2) PN_PUSH(args, SRC(VALUE, PN_STRN(rec->a[k].sv, rec->a[k].iv)));
            else PN_PUSH(args, SRC(VALUE, PN_NIL));
          }
          return CALL(strip_sigil(P, cn), LIST(args));
        }
      named_fallback:
        args = PN_TUP0();
        for (i = 0; i < (int)argn->children.size; i++) {
          PVIPNode *a = argn->children.nodes[i];
          if (a->type == PVIP_NODE_PAIR && a->children.size == 2
              && a->children.nodes[0]->pv) {
            /* key => v becomes a named argument */
            PVIPNode *key = a->children.nodes[0];
            PN_PUSH(args, SRC2(ASSIGN, EXPR(MSG(PN_STRN(key->pv->buf, key->pv->len), PN_NIL)),
                               pvip_to_pn(P, a->children.nodes[1])));
          } else
            PN_PUSH(args, pvip_to_pn(P, a));
        }
      }
      return CALL(strip_sigil(P, node->children.nodes[0]), LIST(args));
    }
    PN obj    = CHILD(0);
    PN method = PN_STRN(node->children.nodes[1]->pv->buf,
                        node->children.nodes[1]->pv->len);
    PN args = PN_TUP0();
    if (NC > 2) {
      PVIPNode *argn = node->children.nodes[2];
      int i;
      if (argn->type == PVIP_NODE_ARGS || argn->type == PVIP_NODE_LIST)
        for (i = 0; i < (int)argn->children.size; i++)
          PN_PUSH(args, pvip_to_pn(P, argn->children.nodes[i]));
      else PN_PUSH(args, pvip_to_pn(P, argn));
    }
    return pvip_send(P, node, obj, method, args);
  }
  case PVIP_NODE_IT_METHODCALL: {
    /* .method(...)  ==  $_.method(...)  (implicit topic invocant) */
    PN obj    = EXPR(MSG(PN_STRN("$_", 2), PN_NIL));
    PN method = PN_STRN(node->children.nodes[0]->pv->buf,
                        node->children.nodes[0]->pv->len);
    PN args = PN_TUP0();
    if (NC > 1) {
      PVIPNode *argn = node->children.nodes[1];
      int i;
      for (i = 0; i < (int)argn->children.size; i++)
        PN_PUSH(args, pvip_to_pn(P, argn->children.nodes[i]));
    }
    return pvip_send(P, node, obj, method, args);
  }
  case PVIP_NODE_RETURN: {
    PN val = NC > 0 ? CHILD(0) : PN_NIL;
    return CALL(PN_return, LIST(PN_TUP(val)));
  }

  /* --- control flow --- */
  case PVIP_NODE_IF: {
    PN result = SRC3(MSG, PN_if, LIST(PN_TUP(CHILD(0))), CHILD(1));
    int i;
    for (i = 2; i < NC; i++)
      PN_PUSH(PN_TUPIF(result), CHILD(i));
    return result;
  }
  case PVIP_NODE_ELSIF:
    return SRC3(MSG, PN_elsif, LIST(PN_TUP(CHILD(0))), CHILD(1));
  case PVIP_NODE_ELSE:
    return SRC3(MSG, PN_else, PN_NIL, CHILD(0));
  case PVIP_NODE_UNLESS: {
    PN result = SRC3(MSG, PN_if, LIST(PN_TUP(SRC(NOT, CHILD(0)))), CHILD(1));
    return result;
  }
  case PVIP_NODE_WHILE:
    return SRC3(MSG, PN_while, LIST(PN_TUP(CHILD(0))), CHILD(1));
  case PVIP_NODE_UNTIL:
    return SRC3(MSG, PN_while, LIST(PN_TUP(SRC(NOT, CHILD(0)))), CHILD(1));
  case PVIP_NODE_FOR: {
    /* for SRC { BODY }  or  for SRC -> $a, $b, ... { BODY }
     * No native iterator protocol in the VM yet: desugar to an indexed
     * while-loop over the (eagerly materialized) source list, binding
     * either the implicit topic $_ or the pointy-block params. */
    PVIPNode *src_node  = node->children.nodes[0];
    PVIPNode *body_node = node->children.nodes[1];
    PVIPNode *stmts_node;
    PN varnames = PN_TUP0();
    if (body_node->type == PVIP_NODE_LAMBDA) {
      PVIPNode *params_node = body_node->children.nodes[0];
      if (params_node && params_node->type == PVIP_NODE_PARAMS) {
        int i;
        for (i = 0; i < (int)params_node->children.size; i++) {
          PVIPNode *p = params_node->children.nodes[i];
          PVIPNode *vt = (p->type == PVIP_NODE_PARAM && p->children.size >= 2)
                         ? p->children.nodes[1] : NULL;
          PN_PUSH(varnames, (vt && vt->pv) ? strip_sigil(P, vt) : PN_STRN("$_", 2));
        }
      }
      stmts_node = (body_node->children.size > 1) ? body_node->children.nodes[1]
                                                    : body_node->children.nodes[0];
    } else {
      stmts_node = body_node;
    }
    if (PN_TUPLE_LEN(varnames) == 0) PN_PUSH(varnames, PN_STRN("$_", 2));

    PN src  = pvip_to_pn(P, src_node);
    PN body = pvip_to_pn(P, stmts_node);

    static int forctr = 0;
    char arrname[32], idxname[32];
    int an = snprintf(arrname, sizeof(arrname), "__p6_for_arr_%d", forctr);
    int in_ = snprintf(idxname, sizeof(idxname), "__p6_for_i_%d", forctr);
    forctr++;
    PN arrv = PN_STRN(arrname, an);
    PN idxv = PN_STRN(idxname, in_);
    long nvars = PN_TUPLE_LEN(varnames), k;

    PN stmts = PN_TUP(SRC2(ASSIGN, EXPR(MSG(arrv, PN_NIL)), src));
    PN_PUSH(stmts, SRC2(ASSIGN, EXPR(MSG(idxv, PN_NIL)), SRC(VALUE, PN_NUM(0))));

    PN loopstmts = PN_TUP0();
    for (k = 0; k < nvars; k++) {
      PN nm  = PN_TUPLE_AT(varnames, k);
      PN off = k == 0 ? EXPR(MSG(idxv, PN_NIL))
                       : SRC2(PLUS, EXPR(MSG(idxv, PN_NIL)), SRC(VALUE, PN_NUM(k)));
      PN idxcall = CALL(PN_STRN("p6_atpos", 8),
                         LIST(TUP2(EXPR(MSG(arrv, PN_NIL)), off)));
      PN_PUSH(loopstmts, SRC2(ASSIGN, EXPR(MSG(nm, PN_NIL)), idxcall));
    }
    /* advance before the body: `next` (continue) jumps to the loop test and
     * must not skip the increment */
    PN_PUSH(loopstmts, SRC2(ASSIGN, EXPR(MSG(idxv, PN_NIL)),
                             SRC2(PLUS, EXPR(MSG(idxv, PN_NIL)), SRC(VALUE, PN_NUM(nvars)))));
    PN_PUSH(loopstmts, body);
    PN loopbody = SRC(BLOCK, loopstmts);
    PN cond = SRC2(LT, EXPR(MSG(idxv, PN_NIL)),
                   CALL(PN_STRN("p6_elems", 8), LIST(PN_TUP(EXPR(MSG(arrv, PN_NIL))))));
    PN_PUSH(stmts, SRC3(MSG, PN_while, LIST(PN_TUP(cond)), loopbody));
    return SRC(CODE, stmts);
  }
  case PVIP_NODE_LAST: return CALL(PN_break, PN_NIL);
  case PVIP_NODE_NEXT: return CALL(PN_continue, PN_NIL);

  /* --- OOP --- */
  case PVIP_NODE_CLASS: {
    /* grammar: children3(CLASS, MAYBE(name), MAYBE(is-superclass), body)
     * Potion classes: the class block is the constructor (ivars `/x = ..`),
     * methods are assigned on the class object after it: `Foo m = (): ..`.
     * So `has` statements go into the constructor body, `method`s are
     * emitted as `Foo name = proto` after the class assignment. */
    PVIPNode *name_node = node->children.nodes[0];
    PVIPNode *body = (NC > 2) ? node->children.nodes[2] : NULL;
    PN name = (name_node && name_node->pv)
               ? PN_STRN(name_node->pv->buf, name_node->pv->len)
               : PN_STRN("__p6_anon_class", 15);
    PN ctor = PN_TUP0();
    PN sig = PN_TUP0();
    p6_class_t *rec = (name_node && name_node->pv)
      ? p6_class_begin(name_node->pv->buf, name_node->pv->len) : NULL;
    PN meths = PN_TUP0();
    if (body && body->type == PVIP_NODE_BLOCK && body->children.size > 0
        && body->children.nodes[0]->type == PVIP_NODE_STATEMENTS) {
      PVIPNode *st = body->children.nodes[0];
      int i;
      for (i = 0; i < (int)st->children.size; i++) {
        PVIPNode *m = st->children.nodes[i];
        if (m->type == PVIP_NODE_HAS) {
          PVIPNode *v = m->children.nodes[0];
          PVIPNode *d = m->children.size > 1 ? m->children.nodes[1] : NULL;
          if (v && v->pv) {
            /* has $.x = d  ==>  optional ctor param `x := d` (flat name, ':',
             * default triple like syntax.y's sig), body `/x = x`, so
             * Foo.new(x => v) overrides the default. Only constant defaults
             * fit a signature; others initialize the ivar unconditionally. */
            PN pn = ivar_name(P, v);
            PN dv = PN_NIL;
            int konst = 1;
            if (d && d->type != PVIP_NODE_NOP) {
              if (d->type == PVIP_NODE_INT)         dv = PN_NUM((int)d->iv);
              else if (d->type == PVIP_NODE_STRING) dv = PN_STRN(d->pv->buf, d->pv->len);
              else konst = 0;
            }
            if (konst) {
              if (rec && rec->n < P6_MAXATTR) {
                p6_attr_t *at = &rec->a[rec->n++];
                size_t l = PN_STR_LEN(pn);
                memset(at, 0, sizeof(*at));
                memcpy(at->name, PN_STR_PTR(pn), l < sizeof(at->name) - 1 ? l : sizeof(at->name) - 1);
                if (d && d->type == PVIP_NODE_INT) { at->kind = 1; at->iv = (long)d->iv; }
                else if (d && d->type == PVIP_NODE_STRING && d->pv->len < sizeof(at->sv)) {
                  at->kind = 2; memcpy(at->sv, d->pv->buf, d->pv->len);
                  at->iv = (long)d->pv->len;
                }
              } else rec = NULL;
              PN_PUSH(sig, pn); PN_PUSH(sig, PN_NUM(':')); PN_PUSH(sig, dv);
              PN_PUSH(ctor, SRC2(ASSIGN, EXPR(SRC(PATH, pn)), EXPR(MSG(pn, PN_NIL))));
            } else
              PN_PUSH(ctor, SRC2(ASSIGN, EXPR(SRC(PATH, pn)), pvip_to_pn(P, d)));
          }
        } else if ((m->type == PVIP_NODE_METHOD || m->type == PVIP_NODE_SUBMETHOD)
                   && m->children.nodes[0]->pv) {
          PN mname = PN_STRN(m->children.nodes[0]->pv->buf, m->children.nodes[0]->pv->len);
          int has_sig = m->children.size > 2;
          PN params = has_sig ? pvip_params(P, m->children.nodes[1])
                              : pvip_params(P, NULL);
          PN mbody = pvip_to_pn(P, m->children.nodes[has_sig ? 2 : 1]);
          PN lhs = SRC(EXPR, TUP2(MSG(name, PN_NIL), MSG(mname, PN_NIL)));
          PN_PUSH(meths, SRC2(ASSIGN, lhs, EXPR(SRC2(PROTO, params, mbody))));
        }
      }
    }
    PN cblk = SRC(BLOCK, ctor);
    PN cls  = SRC3(MSG, PN_class, LIST(sig), cblk);
    PN stmts = PN_TUP0();
    PN_PUSH(stmts, SRC2(ASSIGN, EXPR(MSG(name, PN_NIL)), EXPR(cls)));
    { int i; for (i = 0; i < (int)PN_TUPLE_LEN(meths); i++) PN_PUSH(stmts, PN_TUPLE_AT(meths, i)); }
    return SRC(CODE, stmts);
  }
  case PVIP_NODE_METHOD: {
    /* method outside a class body: plain named sub */
    PN name   = PN_STRN(node->children.nodes[0]->pv->buf,
                        node->children.nodes[0]->pv->len);
    PN params = NC > 2 ? pvip_params(P, node->children.nodes[1]) : LIST(PN_NIL);
    PN body   = NC > 2 ? CHILD(2) : CHILD(1);
    return SRC2(ASSIGN, EXPR(MSG(name, PN_NIL)), EXPR(SRC2(PROTO, params, body)));
  }

  /* --- misc direct mappings --- */
  case PVIP_NODE_DIE:  return p6_call(P, node, "p6_die");
  case PVIP_NODE_USE:  return EXPR(SRC(VALUE, PN_NIL)); /* use v6; use Test; are no-ops for now */
  case PVIP_NODE_REDO: return p6_call(P, node, "p6_redo");

  case PVIP_NODE_PAIR:       return p6_call(P, node, "p6_pair");
  case PVIP_NODE_ATPOS:
    /* @a[i] reads element i (negative counts from the end, beyond: undef) */
    return pvip_send(P, node, CHILD(0), PN_STRN("at", 2), PN_TUP(CHILD(1)));
  case PVIP_NODE_ATKEY:      return p6_call(P, node, "p6_atkey");
  case PVIP_NODE_CONDITIONAL: {
    /* c ?? a !! b: only the chosen branch may run (a native call would
     * evaluate both, and a recursive `$n < 2 ?? $n !! f($n-1)` never ends).
     * Same shape as the p5 ternary: EXPR(if c {a}, else {b}). */
    PN br_t = SRC(BLOCK, PN_TUP(EXPR(CHILD(1))));
    PN br_f = SRC(BLOCK, PN_TUP(EXPR(CHILD(2))));
    PN ift = SRC3(MSG, PN_if, CHILD(0), br_t);
    PN iff = SRC3(MSG, PN_else, PN_NIL, br_f);
    return SRC(EXPR, PN_PUSH(PN_TUP(ift), iff));
  }

  case PVIP_NODE_STRING_CONCAT:    return p6_call(P, node, "p6_concat");
  case PVIP_NODE_STRINGIFY:        return p6_call(P, node, "p6_stringify");
  case PVIP_NODE_INPLACE_CONCAT_S: return p6_call(P, node, "p6_inplace_concat");
  case PVIP_NODE_REPEAT_S:         return p6_call(P, node, "p6_repeat");
  case PVIP_NODE_INPLACE_REPEAT_S: return p6_call(P, node, "p6_inplace_repeat");

  case PVIP_NODE_STREQ: return p6_call(P, node, "p6_streq");
  case PVIP_NODE_STRNE: return p6_call(P, node, "p6_strne");
  case PVIP_NODE_STRLT: return p6_call(P, node, "p6_strlt");
  case PVIP_NODE_STRLE: return p6_call(P, node, "p6_strle");
  case PVIP_NODE_STRGT: return p6_call(P, node, "p6_strgt");
  case PVIP_NODE_STRGE: return p6_call(P, node, "p6_strge");

  case PVIP_NODE_DOR:           return p6_call(P, node, "p6_dor");
  case PVIP_NODE_LOGICAL_XOR:   return p6_call(P, node, "p6_xor");
  case PVIP_NODE_UNARY_BOOLEAN: return p6_call(P, node, "p6_bool");
  case PVIP_NODE_SO:            return p6_call(P, node, "p6_so");

  case PVIP_NODE_INTEGER_DIVISION: return p6_call(P, node, "p6_intdiv");
  case PVIP_NODE_GCD:              return p6_call(P, node, "p6_gcd");
  case PVIP_NODE_LCM:              return p6_call(P, node, "p6_lcm");
  case PVIP_NODE_IS_DIVISIBLE_BY:  return p6_call(P, node, "p6_divisible");
  case PVIP_NODE_NOT_DIVISIBLE_BY: return p6_call(P, node, "p6_not_divisible");

  case PVIP_NODE_RAND: return CALL(PN_STRN("p6_rand", 7), PN_NIL);
  case PVIP_NODE_NOW:  return CALL(PN_STRN("p6_now",  6), PN_NIL);
  case PVIP_NODE_TIME: return CALL(PN_STRN("p6_time", 7), PN_NIL);

  case PVIP_NODE_BEGIN: return p6_call(P, node, "p6_begin");
  case PVIP_NODE_END:   return p6_call(P, node, "p6_end");

  /* --- p6-specific (30%): emit runtime calls into libp6 --- */
  case PVIP_NODE_RANGE:    return p6_call(P, node, "p6_range");
  case PVIP_NODE_REDUCE:   return p6_call(P, node, "p6_reduce");
  case PVIP_NODE_CHAIN: {
    /* In pvip, `a == b` becomes CHAIN[INT(a), EQ(INT(b))].
     * The operator sub-node is partial: it has only the right operand.
     * For a simple 2-element chain, unwrap to a direct binary AST node. */
    if (NC == 2 && node->children.nodes[1]->children.size == 1) {
      PVIPNode *op    = node->children.nodes[1];
      PN l = pvip_to_pn(P, node->children.nodes[0]);
      PN r = pvip_to_pn(P, op->children.nodes[0]);
      switch (op->type) {
        case PVIP_NODE_EQ:  return p6_numeq(P, node, l, r);
        case PVIP_NODE_NE:  return SRC(NOT, p6_numeq(P, node, l, r));
        case PVIP_NODE_LT:  return SRC2(LT,  l, r);
        case PVIP_NODE_LE:  return SRC2(LTE, l, r);
        case PVIP_NODE_GT:  return SRC2(GT,  l, r);
        case PVIP_NODE_GE:  return SRC2(GTE, l, r);
        case PVIP_NODE_CMP:
        case PVIP_NODE_NUM_CMP: return SRC2(CMP, l, r);
        /* string comparison and identity operators: emit p6_* call with both operands */
        case PVIP_NODE_STREQ: return CALL(PN_STRN("p6_streq", 8), LIST(TUP2(l, r)));
        case PVIP_NODE_STRNE: return CALL(PN_STRN("p6_strne", 8), LIST(TUP2(l, r)));
        case PVIP_NODE_STRLT: return CALL(PN_STRN("p6_strlt", 8), LIST(TUP2(l, r)));
        case PVIP_NODE_STRLE: return CALL(PN_STRN("p6_strle", 8), LIST(TUP2(l, r)));
        case PVIP_NODE_STRGT: return CALL(PN_STRN("p6_strgt", 8), LIST(TUP2(l, r)));
        case PVIP_NODE_STRGE: return CALL(PN_STRN("p6_strge", 8), LIST(TUP2(l, r)));
        case PVIP_NODE_SMART_MATCH:     return CALL(PN_STRN("p6_smartmatch", 13), LIST(TUP2(l, r)));
        case PVIP_NODE_NOT_SMART_MATCH: return CALL(PN_STRN("p6_not_smartmatch", 17), LIST(TUP2(l, r)));
        case PVIP_NODE_EQV: return CALL(PN_STRN("p6_eqv", 6), LIST(TUP2(l, r)));
        case PVIP_NODE_VALUE_IDENTITY:     return CALL(PN_STRN("p6_val_eq", 9), LIST(TUP2(l, r)));
        case PVIP_NODE_CONTAINER_IDENTITY: return CALL(PN_STRN("p6_ref_eq", 9), LIST(TUP2(l, r)));
        default: break;
      }
    }
    return p6_call(P, node, "p6_chain");
  }
  case PVIP_NODE_SEQUENCE: return p6_call(P, node, "p6_seq");
  case PVIP_NODE_MINMAX:   return p6_call(P, node, "p6_minmax");
  case PVIP_NODE_Z:        return p6_call(P, node, "p6_zip");

  case PVIP_NODE_SMART_MATCH:     return p6_call(P, node, "p6_smartmatch");
  case PVIP_NODE_NOT_SMART_MATCH: return p6_call(P, node, "p6_not_smartmatch");
  case PVIP_NODE_REGEXP:          return p6_call_str(P, node, "p6_regexp");
  case PVIP_NODE_PERL5_REGEXP:    return p6_call_str(P, node, "p6_rx_p5");

  case PVIP_NODE_WHATEVER:             return CALL(PN_STRN("p6_whatever", 11), PN_NIL);
  case PVIP_NODE_STUB:                 return CALL(PN_STRN("p6_stub",     8),  PN_NIL);
  case PVIP_NODE_UNARY_UPTO:           return p6_call(P, node, "p6_upto");
  case PVIP_NODE_UNARY_FLATTEN_OBJECT: return p6_call(P, node, "p6_flatten");

  case PVIP_NODE_JUNCTIVE_AND:  return p6_call(P, node, "p6_junc_and");
  case PVIP_NODE_JUNCTIVE_OR:   return p6_call(P, node, "p6_junc_or");
  case PVIP_NODE_JUNCTIVE_SAND: return p6_call(P, node, "p6_junc_sand");

  case PVIP_NODE_ROLE:      return p6_call(P, node, "p6_role");
  case PVIP_NODE_IS:        return p6_call(P, node, "p6_is");
  case PVIP_NODE_DOES:      return p6_call(P, node, "p6_does");
  case PVIP_NODE_HAS:       return p6_call(P, node, "p6_has");
  case PVIP_NODE_SUBMETHOD: return p6_call(P, node, "p6_submethod");
  case PVIP_NODE_MULTI:     return p6_call(P, node, "p6_multi");
  case PVIP_NODE_AUGMENT:   return p6_call(P, node, "p6_augment");
  case PVIP_NODE_EXPORT:    return p6_call(P, node, "p6_export");
  case PVIP_NODE_IS_COPY:   return p6_call(P, node, "p6_is_copy");
  case PVIP_NODE_IS_RW:     return p6_call(P, node, "p6_is_rw");
  case PVIP_NODE_IS_REF:    return p6_call(P, node, "p6_is_ref");
  case PVIP_NODE_KEEP:      return p6_call(P, node, "p6_keep");
  case PVIP_NODE_UNDO:      return p6_call(P, node, "p6_undo");
  case PVIP_NODE_NEED:      return p6_call(P, node, "p6_need");
  case PVIP_NODE_PACKAGE:   return p6_call(P, node, "p6_package");
  case PVIP_NODE_MODULE:    return p6_call(P, node, "p6_module");
  case PVIP_NODE_ENUM:      return p6_call(P, node, "p6_enum");

  case PVIP_NODE_TRY:   return p6_call(P, node, "p6_try");
  case PVIP_NODE_REF:   return p6_call(P, node, "p6_ref");
  case PVIP_NODE_BIND:  return p6_call(P, node, "p6_bind");
  case PVIP_NODE_BINDAND_MAKE_READONLY: return p6_call(P, node, "p6_bind_ro");
  case PVIP_NODE_LOGICAL_ANDTHEN:       return p6_call(P, node, "p6_andthen");
  case PVIP_NODE_VALUE_IDENTITY:        return p6_call(P, node, "p6_val_eq");
  case PVIP_NODE_CONTAINER_IDENTITY:    return p6_call(P, node, "p6_ref_eq");
  case PVIP_NODE_EQV:                   return p6_call(P, node, "p6_eqv");
  case PVIP_NODE_LEG:                   return p6_call(P, node, "p6_leg");

  case PVIP_NODE_COMPLEX:              return p6_call(P, node, "p6_complex");
  case PVIP_NODE_SCALAR_DEREF:         return p6_call(P, node, "p6_scalar_deref");
  case PVIP_NODE_ARRAY_DEREF:          return p6_call(P, node, "p6_array_deref");
  case PVIP_NODE_CONTEXTUALIZER_SCALAR:return p6_call(P, node, "p6_ctx_scalar");
  case PVIP_NODE_CONTEXTUALIZER_ARRAY: return p6_call(P, node, "p6_ctx_array");
  case PVIP_NODE_CONTEXTUALIZER_HASH:  return p6_call(P, node, "p6_ctx_hash");

  case PVIP_NODE_META_METHOD_CALL:     return p6_call(P, node, "p6_meta");
  case PVIP_NODE_ATTRIBUTE_VARIABLE:
    return EXPR(SRC(PATH, ivar_name(P, node)));
  case PVIP_NODE_FUNCREF:              return p6_call(P, node, "p6_funcref");
  case PVIP_NODE_VARGS:                return p6_call(P, node, "p6_vargs");
  case PVIP_NODE_PARAM: {
    /* grammar: children4(PARAM, MAYBE(type), var, MAYBE(default), attr)
     * potion_sig_compile's #ifdef P2 branch (core/compile.c) iterates the
     * sig tuple and PN_IS_STR(v)-checks each entry directly to register it
     * as a local -- so each param must be the bare sigil-prefixed name
     * string, not wrapped in anything.
     * vt's type varies: a plain '$x' is a string-leaf (->pv valid); a
     * slurpy '*@x' is PVIP_NODE_VARGS, a *children* node wrapping the
     * real (string-leaf) array_var -- ->pv/->children overlap in the same
     * union, so blindly reading ->pv off a children-node is a garbage
     * pointer deref, not a safe NULL. Dispatch on type instead. */
    PVIPNode *vt = (node->children.size >= 2) ? node->children.nodes[1] : NULL;
    if (vt && vt->type == PVIP_NODE_VARGS && vt->children.size >= 1)
      vt = vt->children.nodes[0];
    if (vt && vt->pv && (vt->type == PVIP_NODE_IDENT || vt->type == PVIP_NODE_VARIABLE
                       || vt->type == PVIP_NODE_ATTRIBUTE_VARIABLE))
      return PN_STRN(vt->pv->buf, vt->pv->len);
    return PN_STRN("$_", 2);
  }
  case PVIP_NODE_OUR:                  return p6_call(P, node, "p6_our");
  case PVIP_NODE_SLANGS:               return p6_call_str(P, node, "p6_slang");
  case PVIP_NODE_PATH:                 return p6_call_str(P, node, "p6_path");

  case PVIP_NODE_BITWISE_OR:  return p6_call(P, node, "p6_bwor");
  case PVIP_NODE_BITWISE_AND: return p6_call(P, node, "p6_bwand");
  case PVIP_NODE_BITWISE_XOR: return p6_call(P, node, "p6_bwxor");
  case PVIP_NODE_UNICODE_CHAR:return p6_call_str(P, node, "p6_chr");

  /* --- magic/special variables --- */
  case PVIP_NODE_STDOUT:   return CALL(PN_STRN("p6_stdout",  9), PN_NIL);
  case PVIP_NODE_STDERR:   return CALL(PN_STRN("p6_stderr",  9), PN_NIL);
  case PVIP_NODE_CLARGS:   return CALL(PN_STRN("p6_argv",    7), PN_NIL);
  case PVIP_NODE_TW_INC:   return CALL(PN_STRN("p6_inc",     6), PN_NIL);
  case PVIP_NODE_TW_ENV:   return CALL(PN_STRN("p6_env",     6), PN_NIL);
  case PVIP_NODE_TW_TMPDIR:return CALL(PN_STRN("p6_tmpdir",  9), PN_NIL);
  case PVIP_NODE_TW_VM:    return CALL(PN_STRN("p6_vm",      5), PN_NIL);
  case PVIP_NODE_TW_OS:    return CALL(PN_STRN("p6_os",      5), PN_NIL);
  case PVIP_NODE_TW_PID:   return CALL(PN_STRN("p6_pid",     6), PN_NIL);
  case PVIP_NODE_TW_CWD:   return CALL(PN_STRN("p6_cwd",     6), PN_NIL);
  case PVIP_NODE_TW_PERLVER: return CALL(PN_STRN("p6_perlver",10), PN_NIL);
  case PVIP_NODE_TW_OSVER:   return CALL(PN_STRN("p6_osver",  8), PN_NIL);
  case PVIP_NODE_TW_EXECUTABLE_NAME: return CALL(PN_STRN("p6_exename", 10), PN_NIL);
  case PVIP_NODE_TW_ROUTINE: return CALL(PN_STRN("p6_routine",10), PN_NIL);
  case PVIP_NODE_TW_PACKAGE: return CALL(PN_STRN("p6_package",10), PN_NIL);
  case PVIP_NODE_TW_CLASS:   return CALL(PN_STRN("p6_class",  8), PN_NIL);
  case PVIP_NODE_TW_MODULE:  return CALL(PN_STRN("p6_module",  9), PN_NIL);
  case PVIP_NODE_TW_A: return EXPR(MSG(PN_STRN("a", 1), PN_NIL));
  case PVIP_NODE_TW_B: return EXPR(MSG(PN_STRN("b", 1), PN_NIL));
  case PVIP_NODE_TW_C: return EXPR(MSG(PN_STRN("c", 1), PN_NIL));

  case PVIP_NODE_SPECIAL_VARIABLE_REGEXP_MATCH:
    return CALL(PN_STRN("p6_match",     8), PN_NIL);
  case PVIP_NODE_SPECIAL_VARIABLE_EXCEPTIONS:
    return CALL(PN_STRN("p6_exception", 12), PN_NIL);

  default:
    fprintf(stderr, "pvip_to_pn: unhandled node type %d\n", (int)node->type);
    return PN_NIL;
  }
}

/** syntax_parse -- entry point exported from libsyntax-p6.so
 *  Same signature as potion_parse / p2_parse.
 *  Parses @src as Perl 6 source and returns a PNSource AST (PN).
 */
PN syntax_parse(Potion *P, PN src, const char *filename) {
  pvip_t *pvip = pvip_new();
  PVIPString *error = NULL;
  const char *s = PN_STR_PTR(src);
  int len = PN_STR_LEN(src);
  PVIPNode *tree;
  PN result;

  DBG_v("\n-- pvip input(%d): %.*s\n", len, len, s);
  // fprintf(stderr, "pvip input(%d): %.*s\n", len, len, s);
  tree = PVIP_parse_string(pvip, s, len, 0, &error);
  if (error || !tree) {
    fprintf(stderr, "%s: p6 parse error: %.*s\n",
            filename, error ? (int)error->len : 0, error ? error->buf : "");
    pvip_free(pvip);
    return PN_NIL;
  }
  result = pvip_to_pn(P, tree);
  /* top-level must be AST_CODE/AST_BLOCK (potion_source_compile rejects
   * anything else, returning PN_NIL, which then crashes potion_jit_proto on
   * a NIL proto) -- a comment/whitespace-only file parses to a bare NOP
   * (or any other single-statement root), so wrap it. */
  if (result != PN_NIL) {
    u8 part = PN_SRC(result)->part;
    if (part != AST_CODE && part != AST_BLOCK)
      result = potion_source(P, AST_CODE, PN_TUP(result), PN_NIL, PN_NIL, 1, PN_NIL);
  }
  pvip_free(pvip);
  DBG_Pvi(result);
  return result;
}
