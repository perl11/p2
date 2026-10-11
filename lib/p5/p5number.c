/**\file lib/p5/p5number.c
  p5 number semantics on top of the core PNNumber (Potion keeps C integer
  division/remainder and integer-only pow): exact integer quotients stay
  integers, else a double (7/2 = 3.5); % has the sign of the right operand;
  ** gives a double for negative or huge exponents; doubles print with %.15g.
  The p2 VM and JIT always dispatch / % ** to these methods.

  (c) 2026 perl11 org */
#include <stdio.h>
#include <math.h>
#include "p5.h"

static PN p5_die(Potion *P, const char *msg) {
  potion_send(P->lobby, PN_STR("die"), PN_STR(msg));
  return PN_NIL;
}

static PN p5_div(Potion *P, PN cl, PN self, PN num) {
  if (PN_IS_INT(self) && PN_IS_INT(num)) {
    long a = PN_INT(self), b = PN_INT(num);
    if (b == 0) return p5_die(P, "Illegal division by zero");
    if (a % b == 0) return PN_NUM(a / b);
    return potion_double(P, (double)a / (double)b);
  }
  if (PN_DBL(num) == 0.0) return p5_die(P, "Illegal division by zero");
  return potion_double(P, PN_DBL(self) / PN_DBL(num));
}

/* operands are truncated to integers, the result has the sign of the right operand */
static PN p5_rem(Potion *P, PN cl, PN self, PN num) {
  long a = PN_IS_INT(self) ? PN_INT(self) : (long)PN_DBL(self);
  long b = PN_IS_INT(num) ? PN_INT(num) : (long)PN_DBL(num);
  long r;
  if (b == 0) return p5_die(P, "Illegal modulus zero");
  r = a % b;
  if (r != 0 && ((r < 0) != (b < 0))) r += b;
  return PN_NUM(r);
}

/* an integer when both are integers, the exponent is >= 0 and the result fits */
static PN p5_pow(Potion *P, PN cl, PN self, PN sup) {
  double z = pow(PN_DBL(self), PN_DBL(sup));
  if (PN_IS_INT(self) && PN_IS_INT(sup) && PN_INT(sup) >= 0 && fabs(z) < 4.0e18) {
    long base = PN_INT(self), e = PN_INT(sup), r = 1;
    while (e) { if (e & 1) r *= base; e >>= 1; if (e) base *= base; }
    return PN_NUM(r);
  }
  return potion_double(P, z);
}

static PN p5_num_string(Potion *P, PN cl, PN self) {
  char buf[40];
  if (PN_IS_INT(self)) sprintf(buf, "%ld", PN_INT(self));
  else sprintf(buf, "%.15g", ((struct PNDouble *)self)->value);
  return potion_str(P, buf);
}

void p5_number_init(Potion *P) {
  PN vts[3];
  int i;
  vts[0] = PN_VTABLE(PN_TNUMBER);
  vts[1] = PN_VTABLE(PN_TINTEGER);
  vts[2] = PN_VTABLE(PN_TDOUBLE);
  for (i = 0; i < 3; i++) {
    potion_method(vts[i], "%", p5_rem, "value=N");
    potion_method(vts[i], "/", p5_div, "value=N");
    potion_method(vts[i], "**", p5_pow, "value=N");
    potion_method(vts[i], "string", p5_num_string, 0);
  }
}
