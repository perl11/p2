#!perl
# p2 regression test: leading `my $x = shift;` statements are plain optional
# parameters; a missing argument is undef, extra arguments are ignored, and a
# sub which mentions @_ afterwards keeps the real @_.
sub two { my $a = shift; my $b = shift; return defined($b) ? "$a,$b" : "$a,undef" }
say two(1);          #=> 1,undef
say two(1, 2);       #=> 1,2
say two(1, 2, 3);    #=> 1,2
sub rest { my $x = shift; return $x . ":" . scalar(@_) }
say rest(5, 6, 7);   #=> 5:2
sub fib { my $n = shift; $n < 2 ? $n : fib($n - 1) + fib($n - 2) }
say fib(15);         #=> 610
