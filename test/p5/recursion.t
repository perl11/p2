#!perl
# p2-specific regression test: recursive subs (also with two recursive calls in
# one expression), qualified sub names, and 'unless caller'.
# Expected stdout is pinned with inline markers, compared by
# test/runtests.sh -p5.
sub fact { my $n = shift; return $n <= 1 ? 1 : $n * fact($n - 1); }
say fact(10); #=> 3628800
sub fib { my $n = shift; return $n < 2 ? $n : fib($n - 1) + fib($n - 2); }
say fib(15); #=> 610
sub Foo::bar { return 7; }
say Foo::bar(); #=> 7
sub run { say "ran"; }
run() unless caller; #=> ran
