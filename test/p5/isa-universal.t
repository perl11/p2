#!perl
# everything isa UNIVERSAL, "@{func(...)}" interpolation
use mro;
package urkkk; @urkkk::ISA = ('klonk', 'kapow');
sub hello { print "hello(@_)\n" }
package main;
my $p = 'urkkk';
print "@{mro::get_linear_isa($p)}|\n"; #=> urkkk klonk kapow|
my $r = bless [], 'klonk';
print $r->isa('UNIVERSAL') ? 1 : 0, UNIVERSAL::isa($r, 'UNIVERSAL') ? 1 : 0, $r->isa('nope') ? 1 : 0, "\n"; #=> 110
$_ = "foobar"; print length(), "|", length, "\n"; #=> 6|6
