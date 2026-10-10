#!perl
# p2-specific regression test: local on a plain package scalar is restored at
# the end of the enclosing block, also with an explicit or implicit sub return
# value; 'our' declarations. (die/last/next skip the restore: known limit.)
# Expected stdout is pinned with inline markers, compared by
# test/runtests.sh -p5.
our $g = 1;
sub show { return $g }
sub f { local $g = 2; return show(); }
sub h { local $g = 3; show() }
say f(); #=> 2
say h(); #=> 3
say $g; #=> 1
{ local $g = 5; say show(); } #=> 5
say $g; #=> 1
our @a;
push @a, 3;
say @a->length; #=> 1
