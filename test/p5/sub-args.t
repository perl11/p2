#!perl
# p2-specific regression test: @_ in plain subs (shift, $_[N], my (...) = @_),
# compared by test/runtests.sh -p5. Plain subs using @_ run in the bytecode VM.
sub two { my ($a, $b) = @_; say "a=$a b=$b"; }
sub idx { say $_[1]; }
sub sh { my $x = shift; my $y = shift; say "x=$x y=$y"; }
sub none { say "none"; }
two(1, 2); #=> a=1 b=2
two(3); #=> a=3 b=undef
idx(5, 6, 7); #=> 6
sh("p", "q"); #=> x=p y=q
none(); #=> none
