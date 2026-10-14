package Foo;
our $count = 3;
our @list = (1,2);
sub new { return bless {}, shift }
sub hi { my $s = shift; return "hi " . helper() . helper() . " $count"; }
sub helper { return "h" }
package main;
print Foo->new->hi, "\n"; #=> hi hh 3
print "$Foo::count @Foo::list $Foo::list[1]\n"; #=> 3 1 2 2
$Foo::count = 9;
print Foo->new->hi, "|", Foo::helper(), "\n"; #=> hi hh 9|h
