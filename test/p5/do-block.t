#!perl
# p2-specific regression test: do BLOCK as a value, and do {} while / until
# (the body always runs once).
# Expected stdout is pinned with inline markers, compared by
# test/runtests.sh -p5.
my $i = 0;
do { $i++; } while ($i < 3);
say $i; #=> 3
my $j = 10;
do { $j++; } while ($j < 3);
say $j; #=> 11
my $k = 0;
do { $k = $k + 2; } until ($k > 5);
say $k; #=> 6
my $v = do { 1; 2; 7 };
say $v; #=> 7
