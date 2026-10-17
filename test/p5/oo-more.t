#!perl
# p2 regression test: AUTOLOAD, @Pkg::ISA and symbolic references, overloaded operators, return if
package Foo;
sub new { bless {}, shift }
sub AUTOLOAD {
  our $AUTOLOAD;
  my $n = $AUTOLOAD; $n =~ s/.*:://;
  return if $n eq "DESTROY";
  return "auto($n) $AUTOLOAD";
}
package A; sub hi { "A" }
package B; our @ISA = ("A");
package Vec;
use overload "+" => sub { Vec->new($_[0]{v} + (ref $_[1] ? $_[1]{v} : $_[1])) },
             "-" => sub { Vec->new($_[0]{v} - $_[1]{v}) },
             "==" => sub { $_[0]{v} == $_[1]{v} },
             q("") => sub { "Vec(" . $_[0]{v} . ")" };
sub new { my ($c, $v) = @_; bless { v => $v }, $c }
package main;
my $o = Foo->new;
say $o->hello;                       #=> auto(hello) Foo::hello
say Foo->world;                      #=> auto(world) Foo::world
say join(",", @B::ISA);              #=> A
{ no strict "refs"; push @{"B::ISA"}, "X"; }
say "@B::ISA";                       #=> A X
sub greet { "hi @_" }
{ no strict "refs"; say &{"greet"}(2), "|", &{"main::greet"}(3); }   #=> hi 2|hi 3
my ($x, $y, $z) = (Vec->new(3), Vec->new(4), Vec->new(3));
say $x + $y, " ", $x + 10, " ", $y - $x;   #=> Vec(7) Vec(13) Vec(1)
say "eq" if $x == $z;                #=> eq
say "ne" unless $x == $y;            #=> ne
sub f { my $n = shift; return if $n eq "D"; return "z$n" }
say f("a"), "|", defined(f("D")) ? "def" : "undef";   #=> za|undef
