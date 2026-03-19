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

static PN pvip_to_pn(Potion *P, PVIPNode *node);

/* strip leading sigil ($, @, %, &) from a pvip string node */
static PN strip_sigil(Potion *P, PVIPNode *node) {
  const char *s = node->pv->buf;
  int len = node->pv->len;
  if (len > 0 && (*s == '$' || *s == '@' || *s == '%' || *s == '&')) {
    s++; len--;
  }
  return PN_STRN((char*)s, len);
}

/* emit a p6 runtime call: p6_name(children...) */
static PN p6_call(Potion *P, PVIPNode *node, const char *name) {
  PN args = PN_TUP0();
  int i;
  for (i = 0; i < NC; i++) PN_PUSH(args, CHILD(i));
  return CALL(PN_STRN((char*)name, strlen(name)), LIST(args));
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
    for (i = 0; i < NC; i++) PN_PUSH(stmts, CHILD(i));
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

  case PVIP_NODE_UNARY_PLUS:  return CHILD(0);
  case PVIP_NODE_UNARY_MINUS: return SRC2(MINUS, SRC(VALUE, PN_NUM(0)), CHILD(0));

  /* --- comparisons --- */
  case PVIP_NODE_EQ:  return SRC2(EQ,  CHILD(0), CHILD(1));
  case PVIP_NODE_NE:  return SRC2(NEQ, CHILD(0), CHILD(1));
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
    PN name = strip_sigil(P, var_node);
    return EXPR(MSG(name, PN_NIL));
  }
  case PVIP_NODE_LIST_ASSIGNMENT:
    return SRC2(ASSIGN, CHILD(0), CHILD(1));

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
    PN name   = PN_STRN(node->children.nodes[0]->pv->buf,
                        node->children.nodes[0]->pv->len);
    PN params = CHILD(1);
    PN body   = CHILD(2);
    return SRC2(ASSIGN, EXPR(MSG(name, PN_NIL)), EXPR(SRC2(PROTO, params, body)));
  }
  case PVIP_NODE_LAMBDA: {
    PN params = NC > 1 ? CHILD(0) : LIST(PN_NIL);
    PN body   = NC > 1 ? CHILD(1) : CHILD(0);
    return SRC2(PROTO, params, body);
  }
  case PVIP_NODE_FUNCALL: {
    PN fname = PN_STRN(node->children.nodes[0]->pv->buf,
                       node->children.nodes[0]->pv->len);
    PN args = PN_TUP0();
    int i;
    for (i = 1; i < NC; i++) PN_PUSH(args, CHILD(i));
    return CALL(fname, LIST(args));
  }
  case PVIP_NODE_METHODCALL: {
    PN obj    = CHILD(0);
    PN method = PN_STRN(node->children.nodes[1]->pv->buf,
                        node->children.nodes[1]->pv->len);
    PN args = PN_TUP0();
    int i;
    for (i = 2; i < NC; i++) PN_PUSH(args, CHILD(i));
    return SRC2(PATH, obj, MSG(method, LIST(args)));
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
  case PVIP_NODE_LAST: return CALL(PN_break, PN_NIL);
  case PVIP_NODE_NEXT: return CALL(PN_STRN("next", 4), PN_NIL);

  /* --- OOP --- */
  case PVIP_NODE_CLASS: {
    PN name = PN_STRN(node->children.nodes[0]->pv->buf,
                      node->children.nodes[0]->pv->len);
    PN body = NC > 1 ? CHILD(1) : SRC(BLOCK, PN_NIL);
    return SRC3(MSG, PN_class, name, body);
  }
  case PVIP_NODE_METHOD: {
    PN name   = PN_STRN(node->children.nodes[0]->pv->buf,
                        node->children.nodes[0]->pv->len);
    PN params = NC > 2 ? CHILD(1) : LIST(PN_NIL);
    PN body   = NC > 2 ? CHILD(2) : CHILD(1);
    return SRC2(ASSIGN, EXPR(MSG(name, PN_NIL)), EXPR(SRC2(PROTO, params, body)));
  }

  /* --- misc direct mappings --- */
  case PVIP_NODE_DIE:  return p6_call(P, node, "p6_die");
  case PVIP_NODE_USE:  return PN_NIL; /* TODO: module loading */
  case PVIP_NODE_REDO: return p6_call(P, node, "p6_redo");

  case PVIP_NODE_PAIR:       return p6_call(P, node, "p6_pair");
  case PVIP_NODE_ATPOS:      return p6_call(P, node, "p6_atpos");
  case PVIP_NODE_ATKEY:      return p6_call(P, node, "p6_atkey");
  case PVIP_NODE_CONDITIONAL:return p6_call(P, node, "p6_conditional");

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
        case PVIP_NODE_EQ:  return SRC2(EQ,  l, r);
        case PVIP_NODE_NE:  return SRC2(NEQ, l, r);
        case PVIP_NODE_LT:  return SRC2(LT,  l, r);
        case PVIP_NODE_LE:  return SRC2(LTE, l, r);
        case PVIP_NODE_GT:  return SRC2(GT,  l, r);
        case PVIP_NODE_GE:  return SRC2(GTE, l, r);
        case PVIP_NODE_CMP:
        case PVIP_NODE_NUM_CMP: return SRC2(CMP, l, r);
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
  case PVIP_NODE_REGEXP:          return p6_call(P, node, "p6_regexp");
  case PVIP_NODE_PERL5_REGEXP:    return p6_call(P, node, "p6_rx_p5");

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
  case PVIP_NODE_ATTRIBUTE_VARIABLE:   return p6_call(P, node, "p6_attr");
  case PVIP_NODE_FUNCREF:              return p6_call(P, node, "p6_funcref");
  case PVIP_NODE_VARGS:                return p6_call(P, node, "p6_vargs");
  case PVIP_NODE_PARAM:                return p6_call(P, node, "p6_param");
  case PVIP_NODE_OUR:                  return p6_call(P, node, "p6_our");
  case PVIP_NODE_SLANGS:               return p6_call(P, node, "p6_slang");
  case PVIP_NODE_PATH:                 return p6_call(P, node, "p6_path");

  case PVIP_NODE_BITWISE_OR:  return p6_call(P, node, "p6_bwor");
  case PVIP_NODE_BITWISE_AND: return p6_call(P, node, "p6_bwand");
  case PVIP_NODE_BITWISE_XOR: return p6_call(P, node, "p6_bwxor");
  case PVIP_NODE_UNICODE_CHAR:return p6_call(P, node, "p6_chr");

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
  pvip_free(pvip);
  DBG_Pvi(result);
  return result;
}
