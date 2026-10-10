/**\file lib/p5/p5eval.c
  p5 eval/die/warn/do/caller.

  (c) 2026 perl11 org */
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

void p5_eval_init(Potion *P) {
  potion_method(P->lobby, "p5eval", p5_eval, "block=&");
  potion_method(P->lobby, "p5do", p5_do, "block=&");
  potion_method(P->lobby, "caller", p5_caller, 0);
  potion_method(P->lobby, "die",  p5_die, "|msg=o");
  potion_method(P->lobby, "warn",  p5_warn, "|msg=o");
  potion_define_global(P, PN_STR("$@"), PN_STR(""));
}
