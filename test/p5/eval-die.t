#!perl
# p2-specific regression test: eval BLOCK returns the block value, die inside
# eval sets $@ (a missing trailing newline is added) and unwinds to the
# innermost eval; $@ is empty after a successful eval. An uncaught die is not
# tested here (it exits 255).
# Expected stdout is pinned with inline markers, compared by
# test/runtests.sh -p5.
my $r = eval { 5 };
say $r; #=> 5
eval { die "boom"; say "not reached"; };
if ($@ eq "boom\n") { say "caught"; } else { say "bad"; } #=> caught
eval { 1 };
say "empty: [$@]"; #=> empty: []
my $v = eval { die "x\n"; 1 } || "default";
say $v; #=> default
eval { eval { die "inner\n" }; if ($@ eq "inner\n") { say "in"; } die "outer\n" }; #=> in
if ($@ eq "outer\n") { say "out"; } #=> out
