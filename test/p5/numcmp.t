#!perl
# p2-specific regression test: relational operators on undef (numifies
# to 0, used to segfault the JIT) and on mixed int/double operands.
# Expected stdout is pinned with inline markers, compared by
# test/runtests.sh -p5.
my $u;
my $r;
$r = $u <= 3; say $r; #=> 1
$r = $u < 3; say $r; #=> 1
$r = $u > 3; say $r; #=> 
$r = $u >= 3; say $r; #=> 
my $i = 3; my $d = 2.5;
$r = $i > $d; say $r; #=> 1
$r = $d > $i; say $r; #=> 
$r = $d <= $d; say $r; #=> 1
