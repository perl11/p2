#!perl
# last/next LABEL leave or continue the labeled enclosing loop; next in a C-style for runs the step.
OUTER: for my $i (1..3) { for my $j (1..3) { next OUTER if $j == 2; print "$i$j "; } }
say ""; #=> 11 21 31 
my $n = 0;
L: while ($n < 5) { $n++; for my $k (1..3) { last L if $n == 3; } print $n; }
say ""; #=> 12
A: for my $i (1..3) { B: for my $j (1..3) { last B if $j == 2; next A if $i == 2; print "$i$j "; } print "e$i "; }
say ""; #=> 11 e1 31 e3 
U: until ($n >= 8) { $n++; for my $k (1..2) { next U if $k == 1; print "x"; } }
say "$n"; #=> 8
OUT: for (my $i = 0; $i < 3; $i++) { for (my $j = 0; $j < 3; $j++) { next OUT if $j == 1; print "$i$j "; } }
say ""; #=> 00 10 20 
for (my $i = 0; $i < 5; $i++) { next if $i == 1; last if $i == 4; print $i; }
say ""; #=> 023
