#!perl
# ** binds tighter than unary minus, * / + -, and is right associative.
# Expected stdout is pinned with inline markers, compared by test/runtests.sh -p5.
say -3**2; #=> -9
say -3**0; #=> -1
say 2**3**2; #=> 512
say 2*2**3; #=> 16
say 1+2**3; #=> 9
say 2**3+1; #=> 9
my $x = 3;
say -$x**2; #=> -9
say 10-2*3; #=> 4
