#!perl
# p2-specific regression test: map, grep and sort with a block ($_ / $a, $b),
# default (string) sort, and list results flattened by map.
# Expected stdout is pinned with inline markers, compared by
# test/runtests.sh -p5.
my @a = (1, 2, 3, 4);
my @d = map { $_ * 2 } @a;
say "@d"; #=> 2 4 6 8
my @g = grep { $_ > 2 } @a;
say "@g"; #=> 3 4
my @s = sort { $b <=> $a } @a;
say "@s"; #=> 4 3 2 1
my @n = sort { $a <=> $b } (10, 9, 100);
say "@n"; #=> 9 10 100
my @t = sort (10, 9, 100);
say "@t"; #=> 10 100 9
my %h = (b => 1, a => 2);
my @k = sort keys %h;
say "@k"; #=> a b
my @p = map { ($_, 1) } (1, 2);
say "@p"; #=> 1 1 2 1
say join(",", map { $_ + 1 } (1, 2, 3)); #=> 2,3,4
my @z = grep { $_ } (0, 1, 2, 0);
say "@z"; #=> 1 2
