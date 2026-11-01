#!perl
# list assignment to hash / array elements, chains and a swap; $h{k} is no whole-hash (slurpy) target
my %h; ($h{a}, $h{b}) = (1, 2);
print "$h{a}$h{b}\n";                        #=> 12
my @a; ($a[0], $a[1]) = (5, 6);
print "@a\n";                                #=> 5 6
my ($x, $y); my @c = (0, 0, 0);
($c[2], $x, $y) = (7, 8, 9);
print "@c|$x$y\n";                           #=> 0 0 7|89
%h = (a => 1, b => 2);
($h{a}, $h{b}) = ($h{b}, $h{a});
print "$h{a}$h{b}\n";                        #=> 21
my $r = { h => {} };
($r->{x}, $r->{h}{y}) = (3, 4);
print "$r->{x}$r->{h}{y}\n";                 #=> 34
my ($p, @q); ($p, @q) = (1, 2, 3);
print "$p|@q\n";                             #=> 1|2 3
my ($k, %w); ($k, %w) = ("z", a => 1);
print "$k|$w{a}\n";                          #=> z|1
