#!perl
# p2-specific regression test: sub prototypes ($$, $;$, ()) are parsed and
# ignored; signatures with names still bind parameters.
# Expected stdout is pinned with inline markers, compared by
# test/runtests.sh -p5.
sub add ($$) { return $_[0] + $_[1]; }
say add(2, 3); #=> 5
sub opt ($;$) { return $_[0]; }
say opt(7); #=> 7
sub none () { return 5; }
say none(); #=> 5
my $f = sub ($) { return $_[0] * 2 };
say $f->(4); #=> 8
sub withsig ($x, $y) { $x - $y }
say withsig(9, 4); #=> 5
sub inc ($) { return $_[0] + 1 }
say &inc(4); #=> 5
my $v = &inc(1);
say $v; #=> 2
