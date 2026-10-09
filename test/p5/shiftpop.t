#!perl
# p2-specific regression test: shift/pop on an explicit array, in both
# the bareword and the parenthesized call form. (Implicit @_/@ARGV
# defaults are not supported, see AGENTS.md.)
# Expected stdout is pinned with inline markers, compared by
# test/runtests.sh -p5.
my @a = (1, 2, 3, 4);
say shift @a; #=> 1
say shift(@a); #=> 2
say pop @a; #=> 4
say pop(@a); #=> 3
