///\file callcc.c
/// creation and calling of continuations, in non-portable asm: x86, x86-64 (SysV and Win64), aarch64
//
// NOTE: these hacks make use of the frame pointer, so they must
// be compiled -fno-omit-frame-pointer!
//
// (c) 2008 why the lucky stiff, the freelance professor
//
#include <stdio.h>
#include "p2.h"
#include "internal.h"
#if (defined(__x86_64__) || defined(_M_X64)) && !defined(_WIN64)
__attribute__((naked, noreturn))
static void potion_x86_64_cont_restore(PN *start, PN *end, PN *stack) {
  __asm__ volatile(
    "mov 0x8(%rdx), %rsp\n"
    "mov 0x10(%rdx), %rbp\n"
    "mov %rdx, %rbx\n"
    "add $0x48, %rdx\n"
    "1:\n"
    "mov (%rdx), %rax\n"
    "add $0x8, %rdi\n"
    "mov %rax, (%rdi)\n"
    "add $0x8, %rdx\n"
    "cmp %rdi, %rsi\n"
    "jne 1b\n"
    "mov 0x18(%rbx), %rax\n"
    "movq $0x0, 0x18(%rbx)\n"
    "mov 0x28(%rbx), %r12\n"
    "mov 0x30(%rbx), %r13\n"
    "mov 0x38(%rbx), %r14\n"
    "mov 0x40(%rbx), %r15\n"
    "mov 0x20(%rbx), %rbx\n"
    "leave\n"
    "ret\n"
  );
}
#elif (defined(__i386__) || defined(_M_IX86)) && !defined(_WIN64)
__attribute__((naked, noreturn))
static void potion_x86_cont_restore(PN *start, PN *end, PN *stack) {
  __asm__ volatile(
    "mov 0x4(%esp), %edi\n"
    "mov 0x8(%esp), %ecx\n"
    "mov 0xc(%esp), %edx\n"
    "mov 0x4(%edx), %esp\n"
    "mov 0x8(%edx), %ebp\n"
    "mov %edx, %esi\n"
    "add $0x1c, %edx\n"
    "1:\n"
    "mov (%edx), %eax\n"
    "add $0x4, %edi\n"
    "mov %eax, (%edi)\n"
    "add $0x4, %edx\n"
    "cmp %edi, %ecx\n"
    "jne 1b\n"
    "mov 0xc(%esi), %eax\n"
    "mov 0x14(%esi), %edi\n"
    "mov 0x18(%esi), %ebx\n"
    "mov 0x10(%esi), %esi\n"
    "leave\n"
    "ret\n"
  );
}
#endif

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
#if (defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)) && !defined(_WIN64)
#if PN_SIZE_T == 8
  potion_x86_64_cont_restore(start, end, cc->stack);
#else
  potion_x86_cont_restore(start, end, cc->stack);
#endif
#elif defined(_WIN64)
  {
    /* Windows commits the stack lazily, one guard page at a time: touch every page between
       the current sp and the lowest address the restore writes, before moving rsp there. */
    volatile char *probe = (volatile char *)&probe;
    char *lo = (char *)start - 4096;
    PN *cs = cc->stack;
    for (; (char *)probe > lo; probe -= 4096) (void)*probe;
    __asm__ ("mov 0x8(%1), %%rsp;"
             "mov %1, %%rbx;"
             "add $0x48, %1;"
          "1:"
             "mov (%1), %%rax;"
             "add $0x8, %0;"
             "mov %%rax, (%0);"
             "add $0x8, %1;"
             "cmp %0, %2;"
             "jne 1b;"
             "mov 0x20(%%rbx), %%rbp;"  /* __builtin_setjmp buffer: fp, label, sp */
             "mov 0x28(%%rbx), %%rax;"
             "mov 0x30(%%rbx), %%rsp;"
             "jmp *%%rax"
             :"+r"(start), "+r"(cs)
             :"r"(end)
             :"%rax", "%rbx", "memory"
            );
  }
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
    fprintf(stderr,"P->mem->cstack=0x%llx ", (unsigned long long)sp1);
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
  cc->len = n + 3 + PN_SAVED_REGS; // must match the allocation size, the gc marks stack+3 .. stack+len
  cc->stack[0] = (PN)sp1;
  cc->stack[1] = (PN)sp2;
  cc->stack[2] = (PN)sp3;
  cc->stack[3] = PN_NIL;
  DBG_vt("\ncallcc: start=%p, end=%p, cc=%p\n", start, sp2, cc->stack);
#if defined(_WIN64)
  /* Win64 gcc keeps %rbp in the middle of the frame, below the saved rbx/rsi/rdi, so the SysV
     "leave; ret" resume does not work. __builtin_setjmp stores rbp, a resume label and rsp in
     stack[4..8], and gcc then treats every register as clobbered at the label: callcc's
     prologue/epilogue save and restore ALL callee-saved registers (incl. xmm6-15) in the frame
     that is copied below. yield jumps to the label with the copied frame, and callcc returns
     through its normal epilogue. */
  if (__builtin_setjmp((void **)(cc->stack + 4)))
    return (PN)cc; /* resumed by potion_continuation_yield */
#elif defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
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
#if defined(__SANITIZE_ADDRESS__)
  {
    PN *s = start + 1; /* stay a loop, not an intercepted memcpy call */
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
