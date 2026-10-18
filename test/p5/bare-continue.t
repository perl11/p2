#!perl
# { BODY } continue { CB }: CB runs after the body and after next, not after last; &{$c}; passes @_ on
my $o = "";
{ $o .= "body,"; } continue { $o .= "cont,"; }
{ $o .= "b2,"; last; } continue { $o .= "NOT,"; }
{ $o .= "b3,"; next; } continue { $o .= "cont3,"; }
print "$o\n"; #=> body,cont,b2,b3,cont3,
sub s1 { &{$_[0]}; }
s1(sub { print "got @_[1,2]\n" }, 7, 8); #=> got 7 8
