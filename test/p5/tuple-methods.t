#!perl
# p2-specific regression test for tuple (list) methods in p5 mode:
# length, first/last, indexing, join, each with a closure, clone, and
# lexicographic cmp. push/shift/pop on p5 arrays are a separate
# still-open gap (see AGENTS.md) and deliberately not exercised here.
# Expected stdout is pinned with inline markers, compared by
# test/runtests.sh -p5.

# Tuple creation and length
my @arr = (1, 2, 3);
my $len = @arr->length;
say $len; #=> 3

# Tuple indexing
my $first = $arr[0];
say $first; #=> 1

my $last = $arr[-1];
say $last; #=> 3

# Tuple first and last methods
my $f = @arr->first;
say $f; #=> 1

my $l = @arr->last;
say $l; #=> 3

# Tuple join
my $joined = @arr->join(", ");
say $joined; #=> 1, 2, 3

# Tuple each with closure
@arr->each(sub ($item) {
    say $item;
});
#=> 1
#=> 2
#=> 3

# Tuple clone
my @clone = @arr->clone;
say @clone->length; #=> 3

# Tuple cmp (lexicographic)
my @other = (1, 2, 4);
my $cmp = @arr cmp @other;
say $cmp; #=> -1
