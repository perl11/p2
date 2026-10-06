#!perl
# p2-specific regression test for basic number operations in p5 mode:
# arithmetic, bitwise, and power operators, plus the string/number
# value conversion builtins in bareword form.

# Arithmetic operators
my $x = 10 + 6;
say $x;

my $y = 10 - 6;
say $y;

my $z = 5 * 3;
say $z;

my $w = 15 / 3;
say $w;

my $r = 10 % 3;
say $r;

# Bitwise operators
my $b1 = 8 << 1;
say $b1;

my $b2 = 16 >> 1;
say $b2;

# Power operator
my $p = 2 ** 3;
say $p;

# String conversion (bareword named-unary form)
my $n1 = 42;
my $str = string $n1;
say $str;

# Number conversion (bareword named-unary form)
my $str2 = "123";
my $num = number $str2;
say $num;

# Number cmp via spaceship
my $cmp = 7 <=> 9;
say $cmp;
