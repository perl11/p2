#!perl
# p2-specific regression test: push/unshift, exists, delete on refs, keys,
# and @$r / %$h dereference (references are tuples/tables).
# Expected stdout is pinned with inline markers, compared by
# test/runtests.sh -p5.
my @a = (1,2);
push @a, 3;
push(@a, 4, 5);
say @a->length; #=> 5
unshift @a, 0;
say $a[0]; #=> 0
my %h = (a=>1, b=>2);
say exists $h{a}; #=> true
say exists $h{zz}; #=> false
my @k = keys(%h); say @k->length; #=> 2
my $r = [7,8,9];
push @$r, 10;
say $r->length; #=> 4
my $hr = {k=>1};
say exists $hr->{k}; #=> true
say exists $hr->{q}; #=> false
delete $hr->{k};
say exists $hr->{k}; #=> false
my @k2 = keys(%$hr); say @k2->length; #=> 0
my @sc = (1, 2, 3);
say scalar(@sc); #=> 3
say scalar @sc; #=> 3
say ucfirst("hello") . lcfirst("WORLD"); #=> HellowORLD
say ref([1]); #=> ARRAY
say ref({a => 1}); #=> HASH
my $one = [7];
say $one->[0]; #=> 7
say ref($one); #=> ARRAY
