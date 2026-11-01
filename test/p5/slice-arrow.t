#!perl
# a list slice may be followed by an arrow: (LIST)[N]->{k} / (LIST)[N]->[M]
my $x = (map { { v => $_ } } 1, 2, 3)[1]->{v};
print "$x\n";                                #=> 2
my $y = ([10, 20, 30], [40, 50])[1]->[0];
print "$y\n";                                #=> 40
print +(1, 2, 3)[1], "\n";                   #=> 2
my @s = (4, 5, 6)[0, 2];
print "@s\n";                                #=> 4 6
