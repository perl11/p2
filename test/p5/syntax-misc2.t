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
