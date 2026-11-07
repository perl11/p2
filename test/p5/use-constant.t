#!perl
# use constant: scalar, hash form (multi line, comments), list, ref and expression values, constants in packages,
# empty prototype subs take no arguments (A + B is a sum), CONST->[i] / CONST->{k}
use strict;
use constant DEBUG => 0;
use constant {
    ALPHA => 'a',
    BETA  => [1, 2, 3],
    GAMMA => { x => 1 },   # a comment, with commas
    DELTA => 2 * 3,
};
use constant E => exp(1);
use constant WEEKDAYS => qw(Mon Tue Wed);
use constant STR => "a => b, c";
print "debug\n" if DEBUG;
print ALPHA, " ", BETA->[1], " ", GAMMA->{x}, " ", DELTA, " ", scalar(@{+BETA}), "\n"; #=> a 2 1 6 3
printf("%.3f\n", E); #=> 2.718
my @d = (WEEKDAYS); print scalar(@d), " $d[1]\n"; #=> 3 Tue
print STR, "\n"; #=> a => b, c
print DELTA + 1, "\n"; #=> 7
package Foo; use constant X => 5; sub get { X + 1 } package main;
print Foo::X, " ", Foo->X, " ", Foo::get(), "\n"; #=> 5 5 6
print "const in str: @{[ DELTA ]}\n"; #=> const in str: 6
if (DELTA == 6) { print "six\n" } #=> six
