#!perl
# `our $x;` inside a sub is the package variable, not a fresh local per call
sub g { our $y; $y++; return $y }
g(); g(); print g(), "\n"; #=> 3
sub n { our $c; return ++$c }
n(); print n(), "\n"; #=> 2
sub s1 { our $acc = "x"; $acc .= "y"; $acc }
print s1(), "\n"; #=> xy
our $top = 5; sub t { our $top; $top++ }
t(); print "$top\n"; #=> 6
sub h { our @l; push @l, 1; scalar @l }
h(); print h(), "\n"; #=> 2
