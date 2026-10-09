#!perl
# p2-specific regression test: array/hash "references" (arrays are tuples,
# hashes are tables): anon {..}, $r->[i], $h->{k}, chains and assignment.
# Expected stdout is pinned with inline markers, compared by
# test/runtests.sh -p5.
my $h = {a => 1, b => 2};
say $h->{a}; #=> 1
my $k = "b";
say $h->{$k}; #=> 2
$h->{c} = 3;
say $h->{c}; #=> 3
my $r = [10, 20, 30];
my $i = 2;
say $r->[1]; #=> 20
say $r->[$i]; #=> 30
$r->[0] = 99;
say $r->[0]; #=> 99
my $n = {x => {y => 5}, l => [7, 8]};
say $n->{x}{y}; #=> 5
say $n->{l}->[1]; #=> 8
