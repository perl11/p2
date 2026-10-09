#!perl
# p2-specific regression test for table (hash) methods in p5 mode:
# literal construction, key access/assignment, length, keys, values
# and delete. Hash key order plus the
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

# delete removes the key and returns the old value
my %del = (a => 1, b => 2, c => 3);
my $old = delete $del{a};
say $old; #=> 1
say $del{a}; #=> undef
delete $del{b};
say $del{b}; #=> undef
say $del{c}; #=> 3
