#!perl
# p2 regression test: a bare shift/pop outside a sub works on @ARGV, and
# shift of an empty list is undef (it used to underflow the length)
@ARGV = ("a", "b", "c");
my $x = shift;
say $x;                       #=> a
say scalar(@ARGV);            #=> 2
my $y = pop;
say $y;                       #=> c
@ARGV = ();
my $z = shift || "dflt";
say $z;                       #=> dflt
say scalar(@ARGV);            #=> 0
sub f { my $p = shift; return $p } say f("in sub"); #=> in sub
