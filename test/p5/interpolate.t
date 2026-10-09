#!perl
# p2-specific regression test: subscripted variable interpolation in
# double-quoted strings ("$a[1]", "$a[$i]", "$h{key}", "$h{$k}").
# Expected stdout is pinned with inline markers, compared by
# test/runtests.sh -p5.
my @a = (10, 20, 30);
my %h = (apple => 5, pear => 6);
my $i = 1;
my $k = "pear";
say "v: $a[1] $a[$i] $h{apple} $h{$k} end"; #=> v: 20 20 5 6 end
say "$a[0]-$a[2]"; #=> 10-30
say "plain $i and [x] {y}"; #=> plain 1 and [x] {y}
say "$i[x]"; #=> 1[x]
