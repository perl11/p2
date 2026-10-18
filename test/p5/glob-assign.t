#!perl
# *name = sub {..} / \&other defines a sub (code ref glob assignment), also in a package
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
