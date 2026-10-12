#!perl
# references: $$r[i] / ${$r}{k} / @{EXPR}, scalar references (\$x aliases, $$r reads and
# writes), code references (\&f, &$c(...)), and flattening of arrays in list literals.
# Expected stdout is pinned with inline markers, compared by test/runtests.sh -p5.
my $ar = [5, 6, 7];
my $hr = {x => 1, y => 2};
say $$ar[0], ${$ar}[1], $$hr{x}, ${$hr}{y}; #=> 5612
$$ar[1] = 9;
say "@$ar"; #=> 5 9 7
say scalar(@{$ar}), $#{$ar}, $#$ar; #=> 322
my $nested = [1, [2, 3]];
say scalar(@{$nested->[1]}), $$nested[1][0]; #=> 22
my $x = 5;
my $sr = \$x;
$$sr = 6;
say "$x|", ref($sr), "|$$sr"; #=> 6|SCALAR|6
$x = 7;
$$sr += 1;
say $x; #=> 8
$$sr++;
say "$x $$sr"; #=> 9 9
my $rr = \$sr;
say ref($rr), $$$rr; #=> REF9
sub inc { my $r = shift; $$r++; }
my $n = 1;
inc(\$n); inc(\$n);
say $n; #=> 3
my $cr = sub { 42 };
sub f { return "f" . ($_[0] || "") }
my $fr = \&f;
say ref($cr), $cr->(), &$cr(), &{$cr}(), ref($fr), $fr->("x"), &$fr("y"); #=> CODE424242CODEfxfy
my @a = (1, 2);
my @l = (0, @a, 3);
say scalar(@l), "@l"; #=> 40 1 2 3
my @m = (@a, @$ar, [8, 9]);
say scalar(@m); #=> 6
my $undef;
my @e = (1, @$undef, 2);
say scalar(@e); #=> 2
for my $el (@$ar) { print $el; }
say ""; #=> 597
$_ = "a:b:c";
my $cnt = split(/:/);
my @sp = split(/:/);
say "$cnt ", scalar(@sp); #=> 3 3
my %cbs = (up => sub { "u" . ($_[0] || "") });
my @cbl = (sub { $_[0] * 2 });
my $cbr = {f => sub { "d" . $_[0] }};
say $cbs{up}->("z"), $cbl[0]->(4), $cbr->{f}->("q"), &{$cbl[0]}(5), &{$cbs{up}}("w"); #=> uz8dq10uw
my $curry = sub { my $x = shift; sub { my $y = shift; sub { $x . $y . shift } } };
say $curry->("a")->("b")->("c"); #=> abc
my $pv = 5; my $pr = \$pv;
my $old = $$pr++;
say "$old $pv"; #=> 5 6
