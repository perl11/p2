#!perl
# p2-specific regression test for infix cmp (both the word form and the
# <=> spaceship form) comparing numbers and strings, plus the string
# builtins length/ord in bareword named-unary form. Paren-call builtins
# (length($s)) are a separate still-open gap (see AGENTS.md) and
# deliberately not exercised here. Expected stdout is pinned with inline
# expected-value markers, compared by test/runtests.sh -p5.

# String cmp
my $cmp1 = "a" cmp "b";
say $cmp1; #=> -1

my $cmp2 = "b" cmp "a";
say $cmp2; #=> 1

my $cmp3 = "a" cmp "a";
say $cmp3; #=> 0

my $cmp4 = "abc" cmp "abd";
say $cmp4; #=> -1

# Numeric spaceship
my $cmp5 = 1 <=> 2;
say $cmp5; #=> -1

my $cmp6 = 10 <=> 2;
say $cmp6; #=> 1

my $cmp7 = 5 <=> 5;
say $cmp7; #=> 0

# Numeric cmp word form
my $cmp8 = 2 cmp 1;
say $cmp8; #=> 1

# String length (bareword named-unary form)
my $s = "hello";
my $len = length $s;
say $len; #=> 5

# String ord (bareword named-unary form)
my $ch = "A";
my $ord = ord $ch;
say $ord; #=> 65

# String concatenation
my $a = "hello";
my $b = "world";
my $c = $a . " " . $b;
say $c; #=> hello world

# cmp in boolean context
if (("a" cmp "b") < 0) {
    say "less"; #=> less
}
if (("b" cmp "a") > 0) {
    say "greater"; #=> greater
}
