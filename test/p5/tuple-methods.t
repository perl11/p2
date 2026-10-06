#!perl
# p2-specific regression test for tuple (list) methods in p5 mode:
# length, first/last, indexing, join, each with a closure, clone, and
# conversion to table. push/shift/pop on p5 arrays are a separate
# still-open gap (see AGENTS.md) and deliberately not exercised here.

# Tuple creation and length
my @arr = (1, 2, 3);
my $len = @arr->length;
say $len;

# Tuple indexing
my $first = $arr[0];
say $first;

my $last = $arr[-1];
say $last;

# Tuple first and last methods
my $f = @arr->first;
say $f;

my $l = @arr->last;
say $l;

# Tuple join
my $joined = @arr->join(", ");
say $joined;

# Tuple each with closure
@arr->each(sub ($item) {
    say $item;
});

# Tuple clone
my @clone = @arr->clone;
say @clone->length;

# Tuple cmp (lexicographic)
my @other = (1, 2, 4);
my $cmp = @arr cmp @other;
say $cmp;
