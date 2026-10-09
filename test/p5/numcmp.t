#!perl
# p2-specific regression test: relational operators on undef (numifies
# to 0, used to segfault the JIT) and on mixed int/double operands.
# Expected stdout is pinned with inline markers, compared by
# test/runtests.sh -p5.
my $u;
my $r;
$r = $u <= 3; say $r; #=> true
$r = $u < 3; say $r; #=> true
$r = $u > 3; say $r; #=> false
$r = $u >= 3; say $r; #=> false
my $i = 3; my $d = 2.5;
$r = $i > $d; say $r; #=> true
$r = $d > $i; say $r; #=> false
$r = $d <= $d; say $r; #=> true
