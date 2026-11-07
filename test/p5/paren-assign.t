#!perl
# an assignment inside parentheses is an assignment (it was a table entry for the compiler):
# `($x = 5);`, `($x //= 5)` as a statement and as a list item, `(my $x //= 4)`, `my ($a) //= 3`
my $a1; ($a1 = 5); print "a1=$a1\n"; #=> a1=5
my $a2 = 1; ($a2 += 5); print "a2=$a2\n"; #=> a2=6
my $a3; ($a3 //= 5); print "a3=$a3\n"; #=> a3=5
my $z; print(($z //= 6), "\n"); #=> 6
(my $x //= 4); print "x=$x\n"; #=> x=4
my ($a) //= 3; print "a=$a\n"; #=> a=3
my ($b) ||= 7; print "b=$b\n"; #=> b=7
my @l = (1, ($a2 = 9), 3); print "@l $a2\n"; #=> 1 9 3 9
