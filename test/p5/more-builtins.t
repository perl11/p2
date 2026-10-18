#!perl
# splice, chop, lvalue/4-arg substr, list repetition, //g in list context, \u \l, $_ defaults, my ($x) = sort.., @{[ ]}
my @a = (1,2,3,4); my @b = splice(@a, 1, 2);
print "@a|@b\n"; #=> 1 4|2 3
@a = (1..5); splice(@a, 1, 0, 9, 8);
print "@a\n"; #=> 1 9 8 2 3 4 5
@a = (1..5); my @c = (7,7); splice(@a, 1, 2, @c); splice(@a, -1);
print "@a\n"; #=> 1 7 7 4
@a = (1..3); my @all = splice(@a);
print scalar(@a), "|@all\n"; #=> 0|1 2 3
my $s = "abc\n"; chomp $s; chop $s;
print "$s\n"; #=> ab
my $t = "abcdef"; substr($t, 0, 2) = "XY"; substr($t, -1) = "Z"; substr($t, 2, 1, "--");
print "$t\n"; #=> XY--deZ
print join(",", (1,2) x 2), "\n"; #=> 1,2,1,2
my @z = (1,2); my @y = (@z) x 2;
print "@y\n"; #=> 1 2 1 2
print join("|", "a1b2c3" =~ /(\d)/g), "\n"; #=> 1|2|3
my $o = ""; for my $w ("a b c" =~ /(\w)/g) { $o .= $w } print "$o\n"; #=> abc
my $u = "abc"; my $v = "XYZ";
print "\u$u \l$v \u\LHELLO wORLD\E!\n"; #=> Abc xYZ Hello world!
for ("Hello") { print lc, uc, length, "\n"; } #=> helloHELLO5
my @w = map { ucfirst } qw(ab cd); print "@w\n"; #=> Ab Cd
my ($min) = sort { $a <=> $b } (3,1,2); my ($first) = reverse (1,2,3);
print "$min $first\n"; #=> 1 3
print "s: @{[ sort { $a <=> $b } (10, 9, 100) ]} m: @{[ map { $_ * 2 } 1..3 ]} n: @{[ 1 + 2 ]}\n"; #=> s: 9 10 100 m: 2 4 6 n: 3
