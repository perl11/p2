#!perl
# p2 regression test: array subscripts take expressions, element assignment
# grows the array, $#a = N, my (..., @a) declares empty arrays, and a plain
# array in boolean context is true iff non-empty.
my ($n, @a, @b);
$n = 2;
$a[$n + 1] = 7;
say scalar(@a);       #=> 4
say $a[$n * 1 + 1];   #=> 7
my @c = (1, 2);
$c[4] = 9;
say scalar(@c);       #=> 5
$#c = 1;
say scalar(@c);       #=> 2
$#c = 2 * $n;
say scalar(@c);       #=> 5
push @b, 1;
say scalar(@b);       #=> 1
my @e;
say "empty" if !@e;   #=> empty
say "full" if @b;     #=> full
say @e ? "t" : "f";   #=> f
say @b ? "t" : "f";   #=> t
say "unl" unless @e;  #=> unl
# numeric strings: index and $#a computed from "6", and == against a number
my $s = "6";
my @g;
$g[1 + $s - 6] = 1;
say scalar(@g);       #=> 2
my @h;
$#h = 2 * $s;
say scalar(@h);       #=> 13
say "eq" if $s == 6;  #=> eq
say "ne" if $s != 7;  #=> ne
say "eq2" if 1.5 == 1.5; #=> eq2
