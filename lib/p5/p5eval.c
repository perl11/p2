/**\file lib/p5/p5eval.c
  p5 eval/die/warn/do/caller.

  (c) 2026 perl11.com */
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include "p5.h"

/* p5 die/eval: eval { BLOCK } runs the block under a setjmp frame; die sets
 * $@ and longjmps to the innermost frame, or prints to stderr and exits 255
 * when there is none. */
typedef struct PNEvalFrame {
  jmp_buf jb;
  struct PNEvalFrame *prev;
} PNEvalFrame;
static PNEvalFrame *eval_top;

static void p5_set_errsv(Potion *P, PN val) {
  potion_define_global(P, potion_str(P, "$@"), val);
}

static PN p5_eval(Potion *P, PN cl, PN self, PN block) {
  PNEvalFrame frame;
  volatile PN result = PN_NIL;
  if (PN_TYPE(block) != PN_TCLOSURE)
    return PN_NIL;
  frame.prev = eval_top;
  eval_top = &frame;
  if (setjmp(frame.jb) == 0) {
    result = PN_CLOSURE_CALL2(P, block, P->lobby, PN_NIL);
    eval_top = frame.prev;
    p5_set_errsv(P, PN_STR(""));
    return result;
  }
  eval_top = frame.prev;
  return PN_NIL;
}

/* eval "STRING": like eval BLOCK, and a syntax error of the string sets $@ too (p5synfail is
 * called by the parser's YY_ERROR while the string is compiled) */
static int eval_str;
static PN p5_evalstr(Potion *P, PN cl, PN self) {
  PNEvalFrame frame;
  volatile PN result = PN_NIL;
  int depth = eval_str;
  struct PNParseRoot *roots = P->parse_roots; /* a syntax error longjmps out of a running parse */
  if (!PN_IS_STR(self)) return PN_NIL;
  frame.prev = eval_top;
  eval_top = &frame;
  eval_str++;
  if (setjmp(frame.jb) == 0) {
    result = potion_send(self, potion_str(P, "eval"));
    eval_top = frame.prev;
    eval_str = depth;
    p5_set_errsv(P, PN_STR(""));
    return result;
  }
  eval_top = frame.prev;
  eval_str = depth;
  P->parse_roots = roots;
  return PN_NIL;
}

static PN p5_synfail(Potion *P, PN cl, PN self, PN msg, PN line) {
  PN text;
  if (!eval_str || !eval_top) return PN_FALSE;
  text = potion_str_format(P, "%s at (eval) line %d.\n", PN_STR_PTR(msg) + (strncmp(PN_STR_PTR(msg), "** ", 3) == 0 ? 3 : 0), (int)PN_INT(line));
  p5_set_errsv(P, potion_str2(P, PN_STR_PTR(text), PN_STR_LEN(text)));
  longjmp(eval_top->jb, 1);
}

/* 'die "x"' self-chains (the string is the receiver), 'die("x")' passes it as
 * the argument */
static PN p5_msg(PN self, PN msg) {
  return PN_IS_STR(msg) ? msg : PN_IS_STR(self) ? self : PN_NIL;
}

/* caller: there is no call-stack introspection; at file level it is empty */
static PN p5_caller(Potion *P, PN cl, PN self) { return PN_NIL; }

/* do BLOCK: the value of the block */
static PN p5_do(Potion *P, PN cl, PN self, PN block) {
  if (PN_TYPE(block) != PN_TCLOSURE) return PN_NIL;
  return PN_CLOSURE_CALL2(P, block, P->lobby, PN_NIL);
}

static PN p5_die(Potion *P, PN cl, PN self, PN msg) {
  msg = p5_msg(self, msg);
  PN text = PN_IS_STR(msg) ? msg : PN_STR("Died\n");
  if (PN_STR_LEN(text) == 0 || PN_STR_PTR(text)[PN_STR_LEN(text) - 1] != '\n')
    { /* potion_str2 interns, so p5 'eq' (identity on interned strings) works */
      PN full = potion_str_format(P, "%s\n", PN_STR_PTR(text));
      text = potion_str2(P, PN_STR_PTR(full), PN_STR_LEN(full));
    }
  if (eval_top) {
    p5_set_errsv(P, text);
    longjmp(eval_top->jb, 1);
  }
  fputs(PN_STR_PTR(text), stderr);
  potion_destroy(P);
  exit(255);
}

static PN p5_warn(Potion *P, PN cl, PN self, PN msg) {
  msg = p5_msg(self, msg);
  PN text = PN_IS_STR(msg) ? msg : PN_STR("Warning: something's wrong\n");
  fputs(PN_STR_PTR(text), stderr);
  if (PN_STR_LEN(text) == 0 || PN_STR_PTR(text)[PN_STR_LEN(text) - 1] != '\n')
    fputc('\n', stderr);
  return PN_TRUE;
}

/* wantarray: the call site sets the context (p5want: 1 list, 0 scalar), a sub that asks takes it at entry
 * and resets it, so a call without an annotation is a list call */
static int p5_want = 1;
static PN p5_want_set(Potion *P, PN cl, PN self, PN ctx) {
  p5_want = PN_IS_INT(ctx) ? (int)PN_INT(ctx) : 1;
  return PN_NIL;
}
static PN p5_want_end(Potion *P, PN cl, PN self, PN a, PN b) {
  p5_want = 1;
  return b;
}
static PN p5_want_get(Potion *P, PN cl, PN self) {
  int w = p5_want;
  p5_want = 1;
  return w == 2 ? PN_NIL : w == 0 ? PN_FALSE : PN_NUM(w); /* 2: void; scalar is false but defined (a string result would be called by the VM); scalar is the empty string (false but defined) */
}

void p5_eval_init(Potion *P) {
  potion_method(P->lobby, "p5eval", p5_eval, "block=&");
  potion_method(P->lobby, "p5evalstr", p5_evalstr, 0);
  potion_method(P->lobby, "p5synfail", p5_synfail, "msg=o,line=o");
  potion_method(P->lobby, "p5do", p5_do, "block=&");
  potion_method(P->lobby, "caller", p5_caller, 0);
  potion_method(P->lobby, "die",  p5_die, "|msg=o");
  potion_method(P->lobby, "p5want", p5_want_set, "ctx=o");
  potion_method(P->lobby, "p5wantget", p5_want_get, 0);
  potion_method(P->lobby, "p5wantend", p5_want_end, "a=o,b=o");
  potion_method(P->lobby, "warn",  p5_warn, "|msg=o");
  potion_define_global(P, PN_STR("$@"), PN_STR(""));
}
