#!perl
# A, B if COND / A, B unless COND: every part runs, last / next in a part work
for my $i (1..4) { print("a$i;"), last if $i == 2; }
print "\n";                                  #=> a2;
for my $i (1..4) { print("b$i;"), next unless $i == 3; print "c$i;"; }
print "\n";                                  #=> b1;b2;c3;b4;
my $n = 0;
print("x"), print("y") if $n == 0;
print("z"), print("w") unless $n == 0;
print "\n";                                  #=> xy
my @l;
push(@l, 1), push(@l, 2), push(@l, 3) if @l == 0;
print scalar(@l), "\n";                      #=> 3
