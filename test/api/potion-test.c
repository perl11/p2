//
// potion-test.c
// tests of the Potion C api
//
// (c) 2008 why the lucky stiff, the freelance professor
// (c) 2013 perl11 org
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "potion.h"
#include "internal.h"
#include "CuTest.h"

PN num = PN_NUM(490);
Potion *P;

void potion_test_nil(CuTest *T) {
  CuAssert(T, "nil isn't a nil type", PN_TYPE(PN_NIL) == PN_TNIL);
  CuAssert(T, "nil is a ref", !PN_IS_PTR(PN_NIL));
  CuAssert(T, "nil nil? is false",
    PN_TRUE == potion_send(PN_NIL, potion_str(P, "nil?")));
}

void potion_test_bool(CuTest *T) {
  CuAssert(T, "true isn't a bool type", PN_TYPE(PN_TRUE) == PN_TBOOLEAN);
  CuAssert(T, "true is a ref", !PN_IS_PTR(PN_TRUE));
  CuAssert(T, "false isn't a bool type", PN_TYPE(PN_FALSE) == PN_TBOOLEAN);
  CuAssert(T, "false is a ref", !PN_IS_PTR(PN_FALSE));
}

void potion_test_int1(CuTest *T) {
  PN zero = PN_ZERO;
  CuAssert(T, "zero isn't zero", PN_INT(zero) == 0);
  CuAssert(T, "zero isn't a number", PN_IS_INT(zero));
  CuAssert(T, "zero is a ref", !PN_IS_PTR(zero));
  CuAssert(T, "zero bad add",
    490 == PN_INT(potion_send(zero, potion_str(P, "+"), num)));
}

void potion_test_int2(CuTest *T) {
  PN pos = PN_NUM(10891);
  CuAssert(T, "positive numbers invalid", PN_INT(pos) == 10891);
  CuAssert(T, "positive not a number", PN_IS_INT(pos));
  CuAssert(T, "positive is a ref", !PN_IS_PTR(pos));
  CuAssert(T, "positive bad add",
    11381 == PN_INT(potion_send(pos, potion_str(P, "+"), num)));
}

void potion_test_int3(CuTest *T) {
  PN neg = PN_NUM(-4343);
  CuAssert(T, "negative numbers invalid", PN_INT(neg) == -4343);
  CuAssert(T, "negative not a number", PN_IS_INT(neg));
  CuAssert(T, "negative is a ref", !PN_IS_PTR(neg));
  CuAssert(T, "negative bad add",
    -3853 == PN_INT(potion_send(neg, potion_str(P, "+"), num)));
}

void potion_test_double(CuTest *T) {
  PN dec = potion_strtod(P, "14466", 5);
  CuAssert(T, "double not a number", PN_TYPE(dec) == PN_TNUMBER);
}

void potion_test_str(CuTest *T) {
  CuAssert(T, "string isn't a string", PN_IS_STR(PN_string));
  CuAssert(T, "string isn't a ref", PN_IS_PTR(PN_string));
  CuAssert(T, "string length isn't working",
    6 == PN_INT(potion_send(PN_string, potion_str(P, "length"))));
}

void potion_test_regex(CuTest *T) {
  PN subject = potion_str(P, "cafe/path-42");
  PN match = potion_regex_match(P, PN_NIL, subject,
                                 potion_str(P, "path-[0-9]+"));
  PN miss = potion_regex_match(P, PN_NIL, subject,
                               potion_str(P, "missing"));
  PN captures = potion_regex_captures(P, PN_NIL, subject,
                                      potion_str(P, "(path)(z)?-([0-9]+)"));
  PN no_captures = potion_regex_captures(P, PN_NIL, subject,
                                         potion_str(P, "missing"));
  PN regex_class = potion_class_find(P, potion_str(P, "Regex"));
  PN compiled = potion_send(regex_class, PN_compile,
                             potion_str(P, "(cafe)"), PN_NUM(8));
  PN replaced = potion_send(compiled, potion_str(P, "replace"),
                             potion_str(P, "CAFE/path"),
                             potion_str(P, "$1$1$1"));

  CuAssert(T, "String.match should return true for a match", match == PN_TRUE);
  CuAssert(T, "String.match should return false for no match", miss == PN_FALSE);
  CuAssert(T, "String.captures should return a tuple", PN_IS_TUPLE(captures));
  CuAssertIntEquals(T, "String.captures tuple length",
                    4, PN_TUPLE_LEN(captures));
  CuAssertStrEquals(T, "path-42", PN_STR_PTR(PN_TUPLE_AT(captures, 0)));
  CuAssertStrEquals(T, "path", PN_STR_PTR(PN_TUPLE_AT(captures, 1)));
  CuAssertStrEquals(T, "42", PN_STR_PTR(PN_TUPLE_AT(captures, 3)));
  CuAssert(T, "unmatched optional capture should be nil",
           PN_TUPLE_AT(captures, 2) == PN_NIL);
  CuAssert(T, "String.captures no-match result should be a tuple",
           PN_IS_TUPLE(no_captures));
  CuAssertIntEquals(T, "String.captures no-match tuple length",
                    0, PN_TUPLE_LEN(no_captures));
  CuAssert(T, "Regex.compile should return a compiled regex",
           PN_VTYPE(compiled) == potion_class_type(P, regex_class));
  CuAssertIntEquals(T, "Regex.compile should retain numeric options",
                    8, PN_INT(potion_send(compiled,
                                         potion_str(P, "options"))));
  CuAssert(T, "compiled regex should match repeatedly",
           potion_send(compiled, potion_str(P, "match"),
                       potion_str(P, "CAFE")) == PN_TRUE &&
           potion_send(compiled, potion_str(P, "match"),
                       potion_str(P, "missing")) == PN_FALSE);
  CuAssertStrEquals(T, "CAFECAFECAFE/path", PN_STR_PTR(replaced));
  potion_send(compiled, potion_str(P, "close"));
}

void potion_test_empty(CuTest *T) {
  PN empty = PN_TUP0();
  CuAssert(T, "empty isn't a tuple", PN_IS_TUPLE(empty));
  CuAssert(T, "empty isn't a ref", PN_IS_PTR(empty));
  CuAssertIntEquals(T, "tuple length is off",
                    0, PN_INT(potion_send(empty, potion_str(P, "length"))));
}

void potion_test_tuple(CuTest *T) {
  PN tup = potion_tuple_with_size(P, 3);
  PN_TUPLE_AT(tup, 0) = PN_NIL;
  PN_TUPLE_AT(tup, 1) = PN_string;
  PN_TUPLE_AT(tup, 2) = tup;
  CuAssert(T, "tuple isn't a tuple", PN_IS_TUPLE(tup));
  CuAssert(T, "tuple isn't a ref", PN_IS_PTR(tup));
  CuAssertIntEquals(T, "tuple length is off",
                    3, PN_INT(potion_send(tup, potion_str(P, "length"))));
}

void potion_test_sig(CuTest *T) {
  // test the simple parser entry point yy_sig, not the compiler transformation potion_sig_compile
  PN sig = potion_sig(P, "num1=N,num2=N");
  CuAssert(T, "signature isn't a tuple", PN_IS_TUPLE(sig));
  CuAssertIntEquals(T, "len=2", 2, PN_INT(PN_TUPLE_LEN(sig)));
  CuAssertIntEquals(T, "arity=2", 2, potion_sig_arity(P, sig));
  CuAssertStrEquals(T, "num1=N,num2=N", //roundtrip
		    PN_STR_PTR(potion_sig_string(P,0,sig)));
  CuAssertStrEquals(T, "(num1, 78, num2, 78)",
		    PN_STR_PTR(potion_send(sig, PN_string)));
  CuAssertStrEquals(T, "num1",
		    PN_STR_PTR(potion_send(PN_TUPLE_AT(sig,0), PN_string)));
  CuAssertIntEquals(T, "num1=N", 'N',
		    PN_INT(PN_TUPLE_AT(sig,1)));
  CuAssertStrEquals(T, "num2",
		    PN_STR_PTR(potion_send(PN_TUPLE_AT(sig,2), PN_string)));
  CuAssertIntEquals(T, "num2=N", 'N',
		    PN_INT(PN_TUPLE_AT(sig,3)));

  sig = potion_sig(P, "x=N|y=N");
  CuAssertStrEquals(T, "(x, 78, 124, y, 78)",
		    PN_STR_PTR(potion_send(sig, PN_string)));
  CuAssertIntEquals(T, "arity=2", 2, potion_sig_arity(P, sig));

  sig = potion_sig(P, "x=N,y=N|r=N");
  CuAssert(T, "signature isn't a tuple", PN_IS_TUPLE(sig));
  CuAssertStrEquals(T, "(x, 78, y, 78, 124, r, 78)",
		    PN_STR_PTR(potion_send(sig, PN_string)));
  CuAssertStrEquals(T, "x=N,y=N|r=N",
		    PN_STR_PTR(potion_sig_string(P,0,sig)));
  CuAssertIntEquals(T, "arity=3", 3, potion_sig_arity(P, sig));
  {
    // roundtrips
    char *sigs[] = {
      "", "x,y", "x", "x=N", "x,y", "x=N,y=o",
      "x|y", "x|y,z", "x=o|y,z", "x|y=o", "x=N,y=N|r=N", /*optional */
      "x:=1", "|x:=1", "x|y:=0", /* defaults */
      "x,y.z", /* the dot */
    };
    int size = sizeof(sigs)/sizeof(char *);
    int i;
    for (i=0; i< size; i++) {
      CuAssertStrEquals(T, sigs[i],
			PN_STR_PTR(potion_sig_string(P,0,potion_sig(P, sigs[i]))));
    }
  }
  CuAssertIntEquals(T, "arity nil", 0, potion_sig_arity(P, PN_NIL));
  // sig "" returns PN_FALSE, which throws an error
  //CuAssertIntEquals(T, "arity ''", 0, potion_sig_arity(P, potion_sig(P, "")));
  CuAssertIntEquals(T, "arity x:=1", 1, potion_sig_arity(P, potion_sig(P, "x:=1")));
  CuAssertIntEquals(T, "arity |x:=1", 1, potion_sig_arity(P, potion_sig(P, "|x:=1")));
  CuAssertIntEquals(T, "arity x|y:=1", 2, potion_sig_arity(P, potion_sig(P, "x|y:=1")));
}

void potion_test_proto(CuTest *T) {
  // test compiler transformation potion_sig_compile, not just yy_sig
  PN p2;
  vPN(Closure) f2;
  vPN(Closure) f1 = PN_CLOSURE(potion_eval(P, potion_str(P, "(x,y):x+y.")));
  CuAssertIntEquals(T, "arity f1", 2, potion_sig_arity(P, f1->sig));
  CuAssertStrEquals(T, "x,y", PN_STR_PTR(potion_sig_string(P,0,f1->sig)));

  p2 = PN_FUNC(PN_CLOSURE_F(f1), "x=N,y=N");
  f2 = PN_CLOSURE(p2);
  CuAssertIntEquals(T, "sig arity f2", 2, potion_sig_arity(P, f2->sig));
  CuAssertStrEquals(T, "x=N,y=N", PN_STR_PTR(potion_sig_string(P,0,f2->sig)));
  CuAssertIntEquals(T, "cl arity f2", 2, PN_INT(potion_closure_arity(P,0,p2)));
}

void potion_test_eval(CuTest *T) {
  PN add, num;
  PN_F addfn;

#if POTION_JIT
  long flags = P->flags;
  if (P->flags & EXEC_JIT) P->flags = (Potion_Flags)((int)P->flags - EXEC_JIT);
#endif
  add = potion_eval(P, potion_str(P, "(x, y): x + y."));
  addfn = PN_CLOSURE_F(add); // c callback
  CuAssertPtrNotNull(T, addfn);
  num = addfn(P, add, 0, PN_NUM(3), PN_NUM(5));
  CuAssertIntEquals(T, "calling closure as c func", 8, PN_INT(num));

  add = potion_eval(P, potion_str(P, "(x=N|y=N): x + y."));
  addfn = PN_CLOSURE_F(add);
  num = addfn(P, add, 0, PN_NUM(3), PN_NUM(5));
  CuAssertIntEquals(T, "calling closure as c func (opt)", 8, PN_INT(num));
  num = addfn(P, add, 1, PN_NUM(3));
  CuAssertIntEquals(T, "optional num = 0", 3, PN_INT(num));

  add = potion_eval(P, potion_str(P, "(x=N,y:=1): x + y."));
  addfn = PN_CLOSURE_F(add);
  num = addfn(P, add, 2, PN_NUM(3), PN_NUM(5));
  CuAssertIntEquals(T, "calling closure as c func (default)", 8, PN_INT(num));
  num = addfn(P, add, 1, PN_NUM(3));
  CuAssertIntEquals(T, "default num = 1", 4, PN_INT(num));

#if POTION_JIT
  P->flags = (Potion_Flags)flags; //restore JIT
  add = potion_eval(P, potion_str(P, "(x, y): x + y."));
  addfn = PN_CLOSURE_F(add); // c callback
  CuAssertPtrNotNull(T, addfn);
  num = addfn(P, add, 0, PN_NUM(3), PN_NUM(5));
  CuAssertIntEquals(T, "calling closure as c func (jit)", 8, PN_INT(num));

#ifdef DEBUG
  //P->flags += DEBUG_COMPILE + DEBUG_JIT;
#endif
  add = potion_eval(P, potion_str(P, "(x=N|y=N): x + y."));
  addfn = PN_CLOSURE_F(add);
  num = addfn(P, add, 0, PN_NUM(3), PN_NUM(5));
  CuAssertIntEquals(T, "calling closure as c func (jit+opt)", 8, PN_INT(num));
  //hard to make this work, would slow it down
  //num = addfn(P, add, 0, PN_NUM(3));
  //CuAssertIntEquals(T, "optional num = 0 (jit)", 3, PN_INT(num));

  add = potion_eval(P, potion_str(P, "(x=N|y:=1): x + y."));
  addfn = PN_CLOSURE_F(add);
  num = addfn(P, add, 0, PN_NUM(3), PN_NUM(5));
  CuAssertIntEquals(T, "calling closure as c func (jit+default)", 8, PN_INT(num));
  //num = addfn(P, add, 0, PN_NUM(3));
  //CuAssertIntEquals(T, "default num = 1 (jit)", 4, PN_INT(num));
#endif

}

#include "gc.h"

void potion_test_allocated(CuTest *T) {
  struct PNMemory *M = P->mem;
  void *prev = NULL;
  PN_SIZE size;
  void *scanptr = (void *)((char *)M->birth_lo + PN_ALIGN(sizeof(struct PNMemory), 8));
  while ((PN)scanptr < (PN)M->birth_cur) {
    if (((struct PNFwd *)scanptr)->fwd != POTION_FWD && ((struct PNFwd *)scanptr)->fwd != POTION_COPIED) {
      if (((struct PNObject *)scanptr)->vt > PN_TUSER &&
          !PN_TYPECHECK(((struct PNObject *)scanptr)->vt)) {
	vPN(Object) o = (struct PNObject *)scanptr;
	fprintf(stderr, "error: scanning heap from %p to %p\n",
		M->birth_lo, M->birth_cur);
	fprintf(stderr, "%p in %s region\n", scanptr,
		IS_GC_PROTECTED(scanptr) ? "protected"
		: IN_BIRTH_REGION(scanptr) ? "birth"
		: IN_OLDER_REGION(scanptr) ? "older"
		: "gc");
	fprintf(stderr, "%p { uniq:0x%08x vt:0x%08x ivars[0]:0x%08lx type:0x%x}\n",
		scanptr, o->uniq, o->vt, o->ivars[0],
		potion_type((PN)scanptr));
	fprintf(stderr, "prev %p: size=%d, type:0x%x (%s)\n",
		prev, potion_type_size(P, prev),
		potion_type((PN)prev), AS_STR(PN_VTABLE(PN_TYPE((PN)prev))));
#ifdef DEBUG
	//potion_dump_stack(P);
#endif
      }
      CuAssert(T, "wrong type for allocated object",
               ((struct PNObject *)scanptr)->vt <= PN_TUSER ||
               PN_TYPECHECK(((struct PNObject *)scanptr)->vt));
    }
    prev = scanptr;
    size = potion_type_size(P, scanptr);
    if (size == 0)
      fprintf(stderr, "zero-sized object %p vt=0x%x type=0x%x\n",
              scanptr, ((struct PNObject *)scanptr)->vt,
              potion_type((PN)scanptr));
    CuAssert(T, "allocated object has zero size", size > 0);
    scanptr = (void *)((char *)scanptr + size);
    CuAssert(T, "allocated object goes beyond GC pointer", (PN)scanptr <= (PN)M->birth_cur);
  }
}

CuSuite *potion_suite() {
  CuSuite *S = CuSuiteNew();
  SUITE_ADD_TEST(S, potion_test_nil);
  SUITE_ADD_TEST(S, potion_test_bool);
  SUITE_ADD_TEST(S, potion_test_int1);
  SUITE_ADD_TEST(S, potion_test_int2);
  SUITE_ADD_TEST(S, potion_test_int3);
  SUITE_ADD_TEST(S, potion_test_double);
  SUITE_ADD_TEST(S, potion_test_str);
  SUITE_ADD_TEST(S, potion_test_regex);
  SUITE_ADD_TEST(S, potion_test_empty);
  SUITE_ADD_TEST(S, potion_test_tuple);
  SUITE_ADD_TEST(S, potion_test_sig);
  SUITE_ADD_TEST(S, potion_test_proto);
  SUITE_ADD_TEST(S, potion_test_eval);
  SUITE_ADD_TEST(S, potion_test_allocated);
  return S;
}

int main(void) {
  POTION_INIT_STACK(sp);
  int count;
  P = potion_create(sp);
  CuString *out = CuStringNew();
  CuSuite *suite = potion_suite();
  CuSuiteRun(suite);
  CuSuiteSummary(suite, out);
  CuSuiteDetails(suite, out);
  printf("%s\n", out->buffer);
  count = suite->failCount;
  CuSuiteFree(suite);
  CuStringFree(out);
  return count;
}
