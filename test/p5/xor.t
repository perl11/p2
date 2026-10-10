#!perl
# p2-specific regression test: the xor operator.
# Expected stdout is pinned with inline markers, compared by
# test/runtests.sh -p5.
my $t = 1;
my $f = 0;
if ($t xor $f) { say "t-f"; } #=> t-f
if ($t xor $t) { say "bad"; } else { say "t-t"; } #=> t-t
if ($f xor $f) { say "bad"; } else { say "f-f"; } #=> f-f
if ($f xor $t) { say "f-t"; } #=> f-t
