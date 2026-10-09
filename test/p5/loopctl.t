#!perl
# p2-specific regression test: last/next in while loops, and a while block
# directly followed by if/else (the 'if' used to be parsed as a statement
# modifier of the while), compared by test/runtests.sh -p5.
$x = 0;
while (1) { $x = $x + 1; last if $x == 3; }
say $x; #=> 3
$x = 0; $y = 0;
while ($x != 3) { $x = $x + 1; next; $y = 9; }
say "$x $y"; #=> 3 0
$x = 5;
while (0) { $x = 1; }
if ($x == 3) { say "wrong"; } else { say "else"; } #=> else
