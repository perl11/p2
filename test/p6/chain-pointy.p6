# chained comparisons, and an unpacking signature `-> ($a, $b)` of a for block (both used to crash)
my $x = 7;
say 5 < $x < 25;                 #=> True
say 5 < $x < 6;                  #=> False
say 1 < $x <= 7 < 9;             #=> True
say 1 < $x == 7;                 #=> True
my @s;
for (1, 2) Z (3, 4) -> ($a, $b) { @s.push($a + $b); }
say @s.join(",");              #=> 4,6
