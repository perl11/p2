#!perl
# p2-specific regression test for table (hash) methods in p5 mode:
# literal construction, key access/assignment, length, keys, and
# values. delete is not yet implemented in the p5 grammar (it parses
# as a no-op bareword call, see AGENTS.md), and hash key order plus the
# hash string representation are allocation-dependent, so none of those
# are pinned here. Expected stdout is pinned with inline markers,
# compared by test/runtests.sh -p5.

# Table creation and at
my %t = (a => 1, b => 2, c => 3);
my $val = $t{a};
say $val; #=> 1

# Table put
$t{d} = 4;
say $t{d}; #=> 4

# Table length
my $len = %t->length;
say $len; #=> 4

# Table keys
my @keys = %t->keys;
say @keys->length; #=> 4

# Table values
my @vals = %t->values;
say @vals->length; #=> 4
