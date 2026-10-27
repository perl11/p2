#!perl
# split with capture groups, /^/, nested list assignment, undef targets, undef as ""
my @c = split(/,|(-)/, "1-10,20,,,");
say scalar(@c); #=> 5
say join("|", @c); #=> 1|-|10||20
say join("|", split(/^/, "ab\ncd\n")) eq "ab\n|cd\n" ? "ok" : "no"; #=> ok
$_ = "abc";
my @z = (@ary = split(//));
say scalar(@z), scalar(@ary); #=> 33
push @ary, 9;
say scalar(@z); #=> 3
(undef, $a, undef, $b) = qw(1 2 3 4);
say "$a|$b"; #=> 2|4
($x, $y) = qw(p q);
say "$x$y"; #=> pq
my $u;
say "a", $u, "b"; #=> ab
say "done"; #=> done
