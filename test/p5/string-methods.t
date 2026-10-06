#!perl
# p2-specific regression test for infix cmp (both the word form and the
# <=> spaceship form) comparing numbers, strings, and tuples, plus the
# string builtins length/ord in bareword named-unary form. Paren-call
# builtins (length($s)) are a separate still-open gap (see AGENTS.md)
# and deliberately not exercised here.

# String cmp
my $cmp1 = "a" cmp "b";
say $cmp1;

my $cmp2 = "b" cmp "a";
say $cmp2;

my $cmp3 = "a" cmp "a";
say $cmp3;

my $cmp4 = "abc" cmp "abd";
say $cmp4;

# Numeric spaceship
my $cmp5 = 1 <=> 2;
say $cmp5;

my $cmp6 = 10 <=> 2;
say $cmp6;

my $cmp7 = 5 <=> 5;
say $cmp7;

# Numeric cmp word form
my $cmp8 = 2 cmp 1;
say $cmp8;

# String length (bareword named-unary form)
my $s = "hello";
my $len = length $s;
say $len;

# String ord (bareword named-unary form)
my $ch = "A";
my $ord = ord $ch;
say $ord;

# String concatenation
my $a = "hello";
my $b = "world";
my $c = $a . " " . $b;
say $c;

# cmp in boolean context
if (("a" cmp "b") < 0) {
    say "less";
}
if (("b" cmp "a") > 0) {
    say "greater";
}
