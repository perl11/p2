#!perl
# ++$x / --$x as a list element or call argument: the new value, not a key => value pair
my $t = 0;
my @l = ("a", ++$t, "b");
print "@l|$t\n";                             #=> a 1 b|1
print "a", ++$t, "b|$t\n";                   #=> a2b|2
my @m = (++$t);
print "@m|$t\n";                             #=> 3|3
my %h = (k => ++$t);
print "$h{k}|$t\n";                          #=> 4|4
my $s = "x" . --$t . "y";
print "$s|$t\n";                             #=> x3y|3
my $i = 0; for (; $i < 3; ++$i) { }
print "$i\n";                                #=> 3
my $j = 0;
print "p$j\n" if ++$j > 0;                   #=> p1
