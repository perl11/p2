#!perl
# undef // 2, undef $x, =~ 'string', sub attributes, sub {...}->()
my $x = undef // 2;
print "$x\n"; #=> 2
my $a = 5; undef $a;
print defined $a ? "def" : "undef", "\n"; #=> undef
print "m\n" if "a.c" =~ '\.'; #=> m
my $v = 'abc';
print "[", ($v !~ 'x'), "]\n"; #=> [1]
sub lv : lvalue { $x }
sub meth :method { "meth" }
print meth(), "\n"; #=> meth
print sub { $_[0] * 2 }->(4), "\n"; #=> 8
my $r = sub : lvalue { 1 };
print "ok\n"; #=> ok
my $ar = [1,2,3]; $#{$ar} = 0;
print scalar(@$ar), "\n"; #=> 1
$#$ar = 3;
print scalar(@$ar), "\n"; #=> 4
our @oa = (1, 2); our @oa;
print scalar(@oa), "\n"; #=> 2
our %oh = (k => 1); our %oh;
print join(",", keys %oh), "\n"; #=> k
my $tx = 1; (($tx > 0) ? $ta : $tb) = 5;
print "$ta|", defined $tb ? "b" : "-", "\n"; #=> 5|-
my ($tp, $tq); my $tz = 0; (($tz > 0) ? $tp : $tq) = 7;
print defined $tp ? "p" : "", defined $tq ? "q" : "", "\n"; #=> q
my $dx = 5; my $dr = \$dx; my @dp = ($dr);
print ${$dp[0]}--, "\n"; #=> 5
print "$dx\n"; #=> 4
${$dr}++;
print "v=${$dr}! ${$dr}\n"; #=> v=5! 5
print qq(q=${$dr}\n); #=> q=5
sub exsub { 1 } package ExPkg; sub exin { 2 } package main;
print exists &exsub ? "y" : "n", exists &nosuch ? "y" : "n", exists &ExPkg::exin ? "y" : "n", "\n"; #=> yny
print defined &exsub ? "y" : "n", defined(&nosuch) ? "y" : "n", "\n"; #=> yn
sub gen_ref { print "# F ${$_[0]} x\n"; } my $gv = 7; gen_ref(\$gv); #=> # F 7 x
