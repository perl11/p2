///\file callcc.c
/// creation and calling of continuations, in non-portable asm, x86 only yet
//
// NOTE: these hacks make use of the frame pointer, so they must
// be compiled -fno-omit-frame-pointer!
//
// (c) 2008 why the lucky stiff, the freelance professor
//
#include <stdio.h>
#include "p2.h"
#include "internal.h"
#if defined(__aarch64__)
__attribute__((naked, noreturn))
static void potion_arm64_cont_restore(PN *start, PN *end, PN *stack) {
  __asm__ volatile(
    "mov x9, x0\n"
    "mov x10, x1\n"
    "mov x11, x2\n"
    "ldr x12, [x11, #8]\n"
    "ldr x29, [x11, #16]\n"
    "mov sp, x12\n"
    "add x9, x9, #8\n"
    "add x12, x11, #112\n"
    "1:\n"
    "ldr x13, [x12], #8\n"
    "str x13, [x9], #8\n"
    "cmp x9, x10\n"
    "b.ne 1b\n"
    "ldr x0, [x11, #24]\n"
    "str xzr, [x11, #24]\n"
    "ldp x19, x20, [x11, #32]\n"
    "ldp x21, x22, [x11, #48]\n"
    "ldp x23, x24, [x11, #64]\n"
    "ldp x25, x26, [x11, #80]\n"
    "ldp x27, x28, [x11, #96]\n"
    "mov sp, x29\n"
    "ldp x29, x30, [sp], #16\n"
    "ret\n"
  );
}
#endif

/**\memberof PNCont
  "yield" method
  \param self PNCont
  \see potion_callcc()
  \returns does not return, continues execution at the position of the given PNCont */
PN potion_continuation_yield(Potion *P, PN cl, PN self) {
  struct PNCont *cc = (struct PNCont *)self;
  PN *start, *end, *sp1 = P->mem->cstack;
#if POTION_STACK_DIR > 0
  start = (PN *)cc->stack[0];
  end = (PN *)cc->stack[1];
#else
  start = (PN *)cc->stack[1];
  end = (PN *)cc->stack[0];
#endif

  if ((PN)sp1 != cc->stack[0]) {
    fprintf(stderr, "** TODO: continuations which switch stacks must be rewritten. (%p != %p)\n",
      sp1, (void *)(cc->stack[0]));
    return PN_NIL;
  }
  DBG_vt("\nyield: start=%p, end=%p, cc=%p\n", start, end, cc->stack);

  //
  // move stack pointer, fill in stack, resume
  cc->stack[3] = (PN)cc;
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
#if PN_SIZE_T == 8
  __asm__ ("mov 0x8(%2), %%rsp;"
           "mov 0x10(%2), %%rbp;"
           "mov %2, %%rbx;"
           "add $0x48, %2;"
        "loop:"
           "mov (%2), %%rax;"
           "add $0x8, %0;"
           "mov %%rax, (%0);"
           "add $0x8, %2;"
           "cmp %0, %1;"
           "jne loop;"
           "mov 0x18(%%rbx), %%rax;"
           "movq $0x0, 0x18(%%rbx);"
           "mov 0x28(%%rbx), %%r12;"
           "mov 0x30(%%rbx), %%r13;"
           "mov 0x38(%%rbx), %%r14;"
           "mov 0x40(%%rbx), %%r15;"
           "mov 0x20(%%rbx), %%rbx;"
           "leave; ret"
           :/* no output */
           :"r"(start), "r"(end), "r"(cc->stack)
           :"%rax", "%rsp", "%rbx"
          );
#else
  __asm__ ("mov 0x4(%2), %%esp;"
           "mov 0x8(%2), %%ebp;"
           "mov %2, %%esi;"
           "add $0x1c, %2;"
        "loop:"
           "mov (%2), %%eax;"
           "add $0x4, %0;"
           "mov %%eax, (%0);"
           "add $0x4, %2;"
           "cmp %0, %1;"
           "jne loop;"
           "mov 0xc(%%esi), %%eax;"
           "mov 0x14(%%esi), %%edi;"
           "mov 0x18(%%esi), %%ebx;"
           "mov 0x10(%%esi), %%esi;"
           "leave; ret"
           :/* no output */
           :"r"(start), "r"(end), "r"(cc->stack)
           :"%eax", "%esp", /*"%ebp",*/ "%esi"
          );
  //DBG_vt("yield => start=%p, end=%p, cc=%p\n", start, end, cc->stack);
#endif
#elif defined(__aarch64__)
  potion_arm64_cont_restore(start, end, cc->stack);
#else
  fprintf(stderr, "** TODO: callcc/yield is unsupported on this architecture.\n");
#endif
#ifdef DEBUG
  if (!P->strings || !P->lobby || !P->mem)
    potion_fatal("fatal: yield stack underflow\n");
#endif
  return self;
}

/**\memberof PNVtable
   global "here" method
   \see potion_continuation_yield()
   \returns a PNCont continuation object which can be yield'ed to later */
ATTRIBUTE_NO_ADDRESS_SAFETY_ANALYSIS
PN potion_callcc(Potion *P, PN cl, PN self) {
  struct PNCont *cc;
  long n;
  PN *start, *sp1 = P->mem->cstack, *sp2, *sp3;
#if defined(DEBUG) && (PN_SIZE_T == 8)
  if ((_PN)sp1 & 0xF) {
    fprintf(stderr,"P->mem->cstack=0x%lx ", (_PN)sp1);
    potion_fatal("stack not 16byte aligned");
  }
#endif
  POTION_ESP(&sp2); // usually P
  POTION_EBP(&sp3);
#if POTION_STACK_DIR > 0
  n = sp2 - sp1;
  start = sp1;
#else
  n = sp1 - sp2 + 1;
  start = sp2;
#endif

  if (n < 0) {
    DBG_vt("\ncallcc: n=%ld, start=%p, end=%p, cc=%p\n", n, start, sp2, sp1);
    potion_fatal("invalid stack direction");
    return 0;
  }
  cc = PN_ALLOC_N(PN_TCONT, struct PNCont, sizeof(PN) * (n + 3 + PN_SAVED_REGS));
  cc->len = n + 3;
  cc->stack[0] = (PN)sp1;
  cc->stack[1] = (PN)sp2;
  cc->stack[2] = (PN)sp3;
  cc->stack[3] = PN_NIL;
  DBG_vt("\ncallcc: start=%p, end=%p, cc=%p\n", start, sp2, cc->stack);
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
#if PN_SIZE_T == 8
  __asm__ ("mov %%rbx, 0x20(%0);"
           "mov %%r12, 0x28(%0);"
           "mov %%r13, 0x30(%0);"
           "mov %%r14, 0x38(%0);"
           "mov %%r15, 0x40(%0);"::"r"(cc->stack));
#else
  __asm__ ("mov %%esi, 0x10(%0);"
           "mov %%edi, 0x14(%0);"
           "mov %%ebx, 0x18(%0)"::"r"(cc->stack));
#endif
#endif
#if defined(__aarch64__)
  __asm__ volatile(
    "stp x19, x20, [%0, #32]\n"
    "stp x21, x22, [%0, #48]\n"
    "stp x23, x24, [%0, #64]\n"
    "stp x25, x26, [%0, #80]\n"
    "stp x27, x28, [%0, #96]\n"
    :
    : "r"(cc->stack)
    : "memory"
  );
#endif

// avoid wrong asan stack underflow, caught in memcpy
#if defined(__clang__) && defined(__SANITIZE_ADDRESS__)
  {
    PN *s = start + 1;
    PN *d = cc->stack + 4 + PN_SAVED_REGS;
    for (int i=0; i < n - 1; i++) {
      *d++ = *s++;
    }
  }
#else
  PN_MEMCPY_N((char *)(cc->stack + 4 + PN_SAVED_REGS), start + 1, PN, n - 1);
#endif
// stack-buffer-underflow sanity check, should not overwrite P
#ifdef DEBUG
  if (!P->strings || !P->lobby || !P->mem)
      potion_fatal("fatal: callcc stack underflow\n");
#endif
  return (PN)cc;
}

// callcc is the "here" method of lobby
void potion_cont_init(Potion *P) {
  PN cnt_vt = PN_VTABLE(PN_TCONT);
  potion_type_call_is(cnt_vt, PN_FUNC(potion_continuation_yield, 0));
}
