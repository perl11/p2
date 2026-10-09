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
