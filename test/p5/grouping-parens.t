#!perl
# p2-specific regression test: '(EXPR)' is plain grouping, not a one-element
# list, on the rhs of a scalar assignment and as a ternary condition.
# Expected stdout is pinned with inline markers, compared by
# test/runtests.sh -p5.
my $f = (1 == 2);
say $f ? "T" : "F"; #=> F
my $t = (1 == 1);
say $t ? "T" : "F"; #=> T
my $n = (3);
say $n; #=> 3
my $a = (1 == 2) ? "yes" : "no";
say $a; #=> no
my $b = (2 == 2) ? "yes" : "no";
say $b; #=> yes
my $c = (1 + 2) * 3;
say $c; #=> 9

# an assignment inside parens is an expression (list item)
my $y;
if (($y = 5) == 5) { say "assigned"; } #=> assigned
if ((my $z = 5) == 5) { say "my-assign"; } #=> my-assign
