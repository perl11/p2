#!perl
# p2-specific regression test for basic number operations in p5 mode:
# arithmetic, bitwise, and power operators, plus the string/number
# value conversion builtins in bareword form. Expected stdout is pinned
# with inline markers, compared by test/runtests.sh -p5.

# Arithmetic operators
my $x = 10 + 6;
say $x; #=> 16

my $y = 10 - 6;
say $y; #=> 4

my $z = 5 * 3;
say $z; #=> 15

my $w = 15 / 3;
say $w; #=> 5

my $r = 10 % 3;
say $r; #=> 1

# Bitwise operators
my $b1 = 8 << 1;
say $b1; #=> 16

my $b2 = 16 >> 1;
say $b2; #=> 8

# Power operator
my $p = 2 ** 3;
say $p; #=> 8

# String conversion (bareword named-unary form)
my $n1 = 42;
my $str = string $n1;
say $str; #=> 42

# Number conversion (bareword named-unary form)
my $str2 = "123";
my $num = number $str2;
say $num; #=> 123

# Number cmp via spaceship
my $cmp = 7 <=> 9;
say $cmp; #=> -1
