/**\file lib/p5/p5.c
  p5 runtime library entry point. The natives live in p5file.c (file handles on
  top of PNFile) and p5table.c (list/hash helpers on top of PNTuple/PNTable).

  (c) 2026 perl11 org */
#include "p5.h"

void Potion_Init_libp5(Potion *P) {
  p5_table_init(P);
  p5_file_init(P);
  p5_string_init(P);
  p5_eval_init(P);
}
