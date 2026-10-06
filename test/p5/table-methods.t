#!perl
# p2-specific regression test for table (hash) methods in p5 mode:
# literal construction, key access/assignment, length, keys, values,
# string, and delete. delete does not return the removed value yet
# (separate open gap), so only its effect on the table is checked.

# Table creation and at
my %t = (a => 1, b => 2, c => 3);
my $val = $t{a};
say $val;

# Table put
$t{d} = 4;
say $t{d};

# Table length
my $len = %t->length;
say $len;

# Table keys
my @keys = %t->keys;
say @keys->length;

# Table values
my @vals = %t->values;
say @vals->length;

# Delete (effect on length only; return value not yet implemented)
delete $t{a};
say %t->length;
