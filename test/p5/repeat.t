#!perl
# p2-specific regression test: the x repetition operator on strings and on
# a parenthesized list in an array assignment.
# Expected stdout is pinned with inline markers, compared by
# test/runtests.sh -p5.
my $s = "a" x 3;
say $s; #=> aaa
say "-" x 5; #=> -----
my $n = 2;
say "ab" x $n; #=> abab
say "a" . "b" x 2; #=> abb
my @a = (1, 2) x 2;
say @a->length; #=> 4
my @b = (0) x 4;
say $b[3]; #=> 0
my %h = (x => 1);
say $h{x}; #=> 1
