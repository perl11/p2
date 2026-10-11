#!perl
# use integer / no integer: a global flag read by the p5 number methods
# (/ truncates, % is the C remainder, + - * truncate double operands).
# Expected stdout is pinned with inline markers, compared by test/runtests.sh -p5.
say 7/2; #=> 3.5
use integer;
say 7/2; #=> 3
say -7/2; #=> -3
say -7%3; #=> -1
say 7%-3; #=> 1
say 7.9+1.9; #=> 8
say 1.5*3; #=> 3
say 2**0.5; #=> 1.4142135623731
no integer;
say 7/2; #=> 3.5
say -7%3; #=> 2
# the pragma is scoped to its block
say 7/2; #=> 3.5
{
    use integer;
    say 7/2; #=> 3
    { no integer; say 9/2; } #=> 4.5
    say 9/2; #=> 4
}
say 9/2; #=> 4.5
sub intdiv { use integer; return $_[0] / $_[1]; }
say intdiv(7, 2); #=> 3
say 7/2; #=> 3.5
# $^H is perl's hints variable: HINT_INTEGER is bit 0, restored at block end
say $^H; #=> 0
{
    use integer;
    say $^H; #=> 1
    $^H &= ~1;
    say 7/2; #=> 3.5
    $^H |= 1;
    say 7/2; #=> 3
}
say $^H; #=> 0
say 5|3; #=> 7
say 5&3; #=> 1
say 5^3; #=> 6
say 12|1&5; #=> 13
