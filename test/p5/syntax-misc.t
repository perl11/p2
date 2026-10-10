#!perl
# p2-specific regression test: trailing commas in lists and calls, POD
# blocks between statements.
# Expected stdout is pinned with inline markers, compared by
# test/runtests.sh -p5.
my %t = (
    a => 1,
    b => 2,
);
say $t{b}; #=> 2
my @l = (1, 2, 3,);
say @l->length; #=> 3

=pod

This is documentation. say "not run";

=cut

say "after pod"; #=> after pod

=head1 More

text

=cut

# a single range / array in parens is the list itself, not a nested tuple;
# big ranges are one allocation
my @r = (1..100000);
say @r->length; #=> 100000
my @s = (3..5);
my @c = (@s);
say @c->length; #=> 3

# chained assignment
my ($ca, $cb);
$ca = $cb = 5;
say $ca + $cb; #=> 10
