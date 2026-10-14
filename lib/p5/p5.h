/**\file lib/p5/p5.h
  p5 runtime library (libp5): perl5 specific natives, kept out of the core.
  Loaded once at startup by front/p2.c; see lib/p5/p5.c.

  (c) 2026 perl11 org */
#ifndef P5_H
#define P5_H

#include "potion.h"

void p5_file_init(Potion *P);
void p5_table_init(Potion *P);
void p5_string_init(Potion *P);
void p5_number_init(Potion *P);
void p5_eval_init(Potion *P);
void p5_ref_init(Potion *P);
void p5_class_init(Potion *P);
void p5_oo_init(Potion *P);
PN p5_call_method(Potion *P, PN closure, PN recv, PN args); /* closure.call(recv, args...) with the signature defaults */
PN p5_class_of(Potion *P, PN v);
PN p5_overload_find(Potion *P, PN obj, const char *key); /* the code of use overload, or nil */
PN p5_overload_str(Potion *P, PN obj); /* "$obj" through an overloaded "", or nil */ /* the package a reference is blessed into, or nil */
PN p5_table_pairs(Potion *P, PN tbl); /* %h in list context: key, value, ... */
PN p5_tostr(Potion *P, PN v); /* string value of v: a code reference is CODE(0x...) */

#endif
