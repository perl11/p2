#!perl
# typeglobs: *name = CODE / \@a / \%h / *other, *{"Pkg::$n"} = sub {..}, *name{SLOT}
sub X::foo { 1 }
*X::bar = sub { 2 };
print X->bar, "\n"; #=> 2
*hello = sub { "hi $_[0]" };
print hello("w"), "\n"; #=> hi w
package Foo; sub a { "A" } *b = \&a;
package main;
print Foo->b, "\n"; #=> A
sub orig { "o" } *alias = \&orig;
print alias(), "\n"; #=> o
my $c = "red"; *{"Foo::$c"} = sub { "<red>" }; $c = "green"; *{"Foo::$c"} = sub { "<green>" };
print Foo->red, Foo->green, "\n"; #=> <red><green>
*{"copy"} = *orig;
print main->copy, "\n"; #=> o
*list = [1, 2, 3];
*dict = { k => "v" };
print "@list $dict{k}\n"; #=> 1 2 3 v
print defined(*orig{CODE}) ? "code" : "nocode", "\n"; #=> code
print defined(*nothing{CODE}) ? "code" : "nocode", "\n"; #=> nocode
