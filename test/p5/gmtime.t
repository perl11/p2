#!perl
# gmtime / localtime: the 9 element list and the scalar string
my @t = gmtime(86400 * 365); print "$t[5] $t[4] $t[3] $t[6] $t[7]\n"; #=> 71 0 1 5 0
print scalar(gmtime(0)), "\n"; #=> Thu Jan  1 00:00:00 1970
my $s = gmtime(86400 * 400); print "$s\n"; #=> Fri Feb  5 00:00:00 1971
my ($sec, $min, $hour, $mday, $mon, $year) = gmtime(3600 * 5 + 61); print "$hour:$min:$sec $mday/$mon/$year\n"; #=> 5:1:1 1/0/70
my @l = localtime(0); print scalar(@l), "\n"; #=> 9
print join(",", (gmtime(1e9))[5, 4, 3]), "\n"; #=> 101,8,9
