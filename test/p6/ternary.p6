# the ternary ?? !! as a value (not a statement): the chosen branch is the
# value. Regression test for the constant-condition bug: a bare-constant
# condition triggered the compiler's constant-if folding, which compiled only
# the chosen branch and left the paired else re-testing a stale condition
# register (ifconst is not shared across the if/else pair) — the wrong branch
# value was returned. Wrapping the condition in EXPR (syn/pvip_to_pn.c) forces
# the dynamic path (condition loaded to reg+1, else re-tests it).
say 1 ?? 5 !! 7;            #=> 5
say 0 ?? 5 !! 7;            #=> 7
say (1 ?? 5 !! 7);          #=> 5
say (0 ?? 5 !! 7);          #=> 7
my $a = 1 ?? 5 !! 7;
say $a;                     #=> 5
my $b = 0 ?? 5 !! 7;
say $b;                     #=> 7
say (1 ?? 5 !! 7) ~ "";     #=> 5
say (0 ?? 5 !! 7) ~ "";     #=> 7
say (1 ?? 5 !! 7) + 1;      #=> 6
say (0 ?? 5 !! 7) + 1;      #=> 8
say (1 ?? "a" !! "b");      #=> a
say (0 ?? "a" !! "b");      #=> b
my $c = 1;
say ($c ?? 5 !! 7);         #=> 5
my $d = 0;
say ($d ?? 5 !! 7);         #=> 7
say (2+3 > 4 ?? 5 !! 7);    #=> 5
say (2+3 > 10 ?? 5 !! 7);   #=> 7
say (1 ?? 100 !! 200) - 100;     #=> 0
say (0 ?? 100 !! 200) - 100;     #=> 100
