#!perl
# p2-specific regression test: calling anonymous subs stored in scalars
# via $cb->(args), including closures over lexicals and several anonymous
# subs (with and without signature) in one file.
# Expected stdout is pinned with inline markers, compared by
# test/runtests.sh -p5.
my $n = 10;
my $add = sub ($x) { $x + $n };
my $none = sub { 42 };
my $sum = sub ($a, $b) { $a + $b };
say $add->(5); #=> 15
say $none->(); #=> 42
say $sum->(3, 4); #=> 7
my $r = $add->(1) + 1;
say $r; #=> 12
