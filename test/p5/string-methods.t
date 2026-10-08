#!perl
# p2-specific regression tests for string operations: infix cmp, string
# builtins in bareword and paren-call form, concatenation, and q/qq
# quote-like operators. Expected stdout is pinned with inline markers,
# compared by test/runtests.sh -p5.

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

# String length (paren form, self-chains to the string)
my $len2 = length($s);
say $len2; #=> 5

# String ord (bareword named-unary form)
my $ch = "A";
my $ord = ord $ch;
say $ord; #=> 65

# String ord (paren form)
my $ord2 = ord($ch);
say $ord2; #=> 65

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

# Interpolating quote-like operators, including balanced inner delimiters.
my $who = "world";
say qq/slash $who/; #=> slash world
say qq(paren ($who)); #=> paren (world)
say qq[square [$who]]; #=> square [world]
say qq{brace {$who}}; #=> brace {world}

# Whitespace between qq and its delimiter is legal, and the closing
# delimiter may be followed by whitespace before the statement separator.
print qq
[multiline $who
]
; #=> multiline world

# Non-interpolating quote-like operators preserve ordinary backslashes and
# raw variables while allowing escaped delimiters.
say q/slash \/ $who/; #=> slash / $who
say q(paren (raw $who)); #=> paren (raw $who)
say q[square [raw $who]]; #=> square [raw $who]
say q{brace {raw $who}}; #=> brace {raw $who}
say q<angle <raw $who>>; #=> angle <raw $who>
