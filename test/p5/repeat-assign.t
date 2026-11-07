#!perl
# $s x= N, a number on the left of x repeats its string form
my $s = "ab"; $s x= 3; print "$s\n"; #=> ababab
my $n = 2; $n x= 3; print "$n\n"; #=> 222
my %h = (k => "-"); $h{k} x= 4; print "$h{k}\n"; #=> ----
print 2 x 3, "|", 1.5 x 2, "\n"; #=> 222|1.51.5
