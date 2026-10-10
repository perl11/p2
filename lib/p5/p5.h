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
void p5_eval_init(Potion *P);

#endif
