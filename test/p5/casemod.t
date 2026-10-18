#!perl
# "\Q..\E" "\U..\E" "\L..\E" in double quoted strings, and a for modifier over a bare comma list
my $v = "a.b";
print "\Qa.b*\E|\Uabc\E|\LABC\E|\Q$v\E|x\Uy$v\Ez\n"; #=> a\.b\*|ABC|abc|a\.b|xYA.Bz
print qq(\Q1+1\E\n); #=> 1\+1
print "plain $v\n"; #=> plain a.b
my %h = (foo => 1);
delete $h{foo} for $h{foo}, 1;
print scalar(keys %h), "\n"; #=> 0
print "$_ " for 1, 2, 3; print "\n"; #=> 1 2 3 
