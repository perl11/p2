#!perl
# p2-specific regression test: C-style for (INIT; COND; STEP), including
# empty parts. (next skips STEP: known limit, not tested.)
# Expected stdout is pinned with inline markers, compared by
# test/runtests.sh -p5.
my $s = 0;
for (my $i = 0; $i < 5; $i++) { $s = $s + $i; }
say $s; #=> 10
my $j;
for ($j = 3; $j > 2; $j--) { say $j; } #=> 3
my $n = 0;
for (;;) { $n++; if ($n == 3) { last; } }
say $n; #=> 3
my $k = 0;
for (; $k < 2;) { $k++; }
say $k; #=> 2
