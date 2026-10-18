#!perl
# reverse LIST, scalar reverse(), undef @a/%h, low precedence not, $#a++/--, qw in for lists, $] $^O
my $r = ""; for (reverse 1,2,3) { $r .= $_ } print "$r\n"; #=> 321
print scalar reverse("abc"), "\n"; #=> cba
my @a = (1,2,3); my %h = (a=>1); undef @a; undef %h; print scalar(@a), scalar(keys %h), "\n"; #=> 00
print "ok\n" if "abc" =~ /b/ and not "x" eq "y"; #=> ok
my @b = (1,2,3,4); --$#b; $#b--; print scalar(@b), "\n"; #=> 2
for my $x ("a", qw(b c), "d") { print $x } print "\n"; #=> abcd
print $] >= 5.006 ? "new" : "old", " ", $^W, "\n"; #=> new 0
$, = "-"; print "[$,]\n"; #=> [-]
my @pa = (1); my @pb = (2, 3); my $pc = push @pa, @pb, (), 4; print "$pc @pa\n"; #=> 4 1 2 3 4
$pc = unshift @pa, qw(x y); print "$pc @pa\n"; #=> 6 x y 1 2 3 4
my @sp = (1..5); my $sx = splice(@sp, 1, 2); print "$sx|@sp|", join(":", splice(@sp, -1)), "\n"; #=> 3|1 4 5|5
