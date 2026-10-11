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
