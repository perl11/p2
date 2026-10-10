#!perl
# p2-specific regression test: statement modifiers 'while' and 'until' on
# expression and assignment statements.
# Expected stdout is pinned with inline markers, compared by
# test/runtests.sh -p5.
my $x = 0;
$x++ while $x < 5;
say $x; #=> 5
my $y = 0;
$y = $y + 2 until $y > 6;
say $y; #=> 8
my $z = 0;
$z++ until $z == 3;
say $z; #=> 3
$z = $z + 10 while $z < 30;
say $z; #=> 33
