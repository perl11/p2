#!perl
# defined($k = each %h): the right side is in scalar context (the key), so the loop ends
my %h = (a => 1);
my ($k, $n) = (undef, 0);
while (defined($k = each %h)) { $n++; last if $n > 5 }
print "keys: $n\n";    #=> keys: 1
my @q = (7, 8);
my $s = 0;
while (defined($k = shift @q)) { $s += $k }
print "sum: $s\n";     #=> sum: 15
