#!perl
# array and hash slices: @a[LIST], @h{LIST}, @$r[LIST], @{$r}{LIST}
# Expected stdout is pinned with inline markers, compared by test/runtests.sh -p5.
my @sl = (10, 20, 30, 40);
say join(",", @sl[1,2]), "|", join(",", @sl[1..2]), "|", join(",", @sl[-2,-1]); #=> 20,30|20,30|30,40
my %sh = (a => 1, b => 2, c => 3);
say join(",", @sh{qw(a c)}); #=> 1,3
my $slr = [5, 6, 7];
my $slh = {x => 1, y => 2};
say join(",", @$slr[0,2]), "|", join(",", @{$slr}[1,2]), "|", join(",", @$slh{qw(x y)}); #=> 5,7|6,7|1,2
