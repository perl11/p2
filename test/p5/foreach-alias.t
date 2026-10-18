#!perl
# the foreach variable aliases the elements of a plain array: changes are written back
my @a = (1,2,3);
for my $x (@a) { $x *= 2 }
print "@a\n"; #=> 2 4 6
$_ += 10 for @a;
print "@a\n"; #=> 12 14 16
for my $x (@a) { $x = "n$x"; last if $x eq "n14" }
print "@a\n"; #=> n12 n14 16
my @b = (1,2,3); for my $y (@b) { next if $y == 2; $y++ }
print "@b\n"; #=> 2 2 4
my @c = ("a b", "c"); for (@c) { s/ /_/ }
print "@c\n"; #=> a_b c
our @e = (5,6); for $q (@e) { $q = 0 }
print "@e\n"; #=> 0 0
my @s; my @n = (1,2); for my $k (@n) { push @s, sub { $k } }
print $s[0]->(), $s[1]->(), "\n"; #=> 12
