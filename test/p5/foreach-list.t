#!perl
# foreach over list-returning calls, reverse of a range, for our $i, bare eof
sub bar { return (1, 2, 4) }
sub one { 5 }
my @array = ('A', 'B', 'C');
$a = 0; foreach $b (bar()) { $a += $b } print "$a\n"; #=> 7
for (one()) { print "[$_]" } print "\n"; #=> [5]
$foo = 3210; for (split(' ', 'a b c')) { $foo .= $_ } print "$foo\n"; #=> 3210abc
for (reverse 1..3) { print } print "\n"; #=> 321
my $r = ''; for (reverse 1 .. 3, @array) { $r .= $_ } print "$r\n"; #=> CBA321
$r = ''; for our $i (reverse @array) { $r .= $i } print "$r\n"; #=> CBA
open(my $fh, '<', $0) or die; my $n = 0;
while (<$fh>) { last if $n++ > 1 } print eof ? "eof" : "more", "\n"; #=> more
while (<$fh>) { } print eof ? "eof" : "more", "\n"; #=> eof
