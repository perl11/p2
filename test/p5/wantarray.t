#!perl
# wantarray: list and scalar from the assignment, void from a statement
sub ctx { my $w = wantarray; if (!defined $w) { return "void" } if ($w) { return "list" } return "scalar" }
my @a = ctx(); my $s = ctx();
print "$a[0] $s\n"; #=> list scalar
sub v { print defined(wantarray) ? "d" : "void", "\n"; } v(); #=> void
my ($x) = ctx(); print "$x\n"; #=> list
print ctx(), "\n"; #=> list
sub outer { return ctx() } my $o = outer(); print "$o\n"; #=> scalar
sub ret { return wantarray ? (1, 2, 3) : "one" } my @l = ret(); my $c = ret();
print scalar(@l), " $c\n"; #=> 3 one
print "main: ", defined(wantarray) ? "def" : "undef", "\n"; #=> main: undef
