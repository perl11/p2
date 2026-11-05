#!perl
# p2 regression test: `my $x OP= v` declares $x (undef) and applies the compound assignment
my $t //= 3;  say "t=$t";   #=> t=3
my $u ||= 4;  say "u=$u";   #=> u=4
my $v += 5;   say "v=$v";   #=> v=5
my $w .= "b"; say "w=$w";   #=> w=b
my $y -= 2;   say "y=$y";   #=> y=-2
