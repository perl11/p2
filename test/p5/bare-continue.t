#!perl
# { BODY } continue { CB }: CB runs after the body and after next, not after last; &{$c}; passes @_ on
my $o = "";
{ $o .= "body,"; } continue { $o .= "cont,"; }
{ $o .= "b2,"; last; } continue { $o .= "NOT,"; }
{ $o .= "b3,"; next; } continue { $o .= "cont3,"; }
print "$o\n"; #=> body,cont,b2,b3,cont3,
sub s1 { &{$_[0]}; }
s1(sub { print "got @_[1,2]\n" }, 7, 8); #=> got 7 8
sub f { { 1; } continue { return 20; } return 5; }
print f(), "\n"; #=> 20
sub g { my $i = 0; while ($i++ < 3) { next if $i == 1; } continue { return "c$i" if $i == 2; } return "none"; }
print g(), "\n"; #=> c2
