#!perl
# qr / m with an = delimiter; an element read as the match pattern
my $r = qr=crumpets=;
print "m1\n" if " crumpets " =~ $r;          #=> m1
my @a = (qr/crumpets/);
print "m2\n" if " crumpets " =~ $a[0];       #=> m2
my %h = (re => qr/^ab/);
print "m3\n" if "abc" =~ $h{re};             #=> m3
sub f { print "m4\n" if " crumpets " =~ $_[0] }
f(qr/crumpets/);                             #=> m4
print "n1\n" unless "xyz" =~ $a[0];          #=> n1
