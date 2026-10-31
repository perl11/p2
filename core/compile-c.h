/** Support for C emitted by the compile/c backend. */
#ifndef POTION_COMPILE_C_H
#define POTION_COMPILE_C_H
#include <stdio.h>
#include <math.h>
#include "p2.h"
#include "internal.h"
#include "asm.h"

/* One slot per named instruction, not per possible callee parameter. */
typedef struct { int active, base, index; PN value; } PNCNamed;
PN potion_c_invoke(Potion *, PN *, int, int, PNCNamed *, int);
/* fill args[0..to-from) with the signature defaults of parameters from..to */
void potion_c_defaults(PN sig, int from, int to, PN *args);

/* Keep integer arithmetic identical to the VM, including tagged-range overflow. */
#ifdef P2
#define PN_C_OVERFLOW(name, x, y, oper) potion_obj_##name(P, x, y)
#else
#define PN_C_OVERFLOW(name, x, y, oper) potion_double(P, PN_DBL(x) oper PN_DBL(y))
#endif
#define PN_C_MATH(name, oper, builtin) \
static inline PN pn_c_##name(Potion *P, PN x, PN y) { \
  intptr_t z; \
  if (!PN_IS_INT(x) || !PN_IS_INT(y)) return potion_obj_##name(P, x, y); \
  if (__builtin_##builtin##_overflow(PN_INT(x), PN_INT(y), &z) \
      || z > PN_INT(INTPTR_MAX) || z < PN_INT(INTPTR_MIN)) \
    return PN_C_OVERFLOW(name, x, y, oper); \
  return PN_NUM(z); \
}
PN_C_MATH(add, +, add)
PN_C_MATH(sub, -, sub)
PN_C_MATH(mult, *, mul)
#undef PN_C_MATH
#undef PN_C_OVERFLOW

#ifdef P2
/* Initialize the p5/p6 runtime library. Elsewhere it is linked (-lp5, -lp6); on macOS they are
 * loadable bundles, which the linker cannot link, so they are found by the loader path. */
# ifdef __APPLE__
#  include <dlfcn.h>
#  include <stdlib.h>
static void pn_c_init_rt(Potion *P, char *lib, const char *init) {
  void *h = potion_dlopen_find(P, lib);
  void (*fn)(Potion *) = h ? (void (*)(Potion *))dlsym(h, init) : NULL;
  if (!fn) { fprintf(stderr, "** the %s runtime is not found\n", lib); exit(1); }
  fn(P);
}
#  define PN_C_INIT_RT(P, lib, fn) pn_c_init_rt(P, lib, #fn)
# else
#  define PN_C_INIT_RT(P, lib, fn) do { extern void fn(Potion *); fn(P); } while (0)
# endif
#endif
#endif
