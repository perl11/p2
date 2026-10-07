#!perl
# Native p5 heredocs: literal, interpolated, backslash-delimited, and
# multiple queued bodies on one introducer line.

my $value = "expanded";
print <<'LITERAL';
literal $value \\n
LITERAL

print <<INTERPOLATED;
$value
INTERPOLATED

print <<'LEFT' . <<\RIGHT;
left
LEFT
right
RIGHT

print <<QUOTES;
plain " quoted; escaped \" quoted; slash \\ then quote "
QUOTES

# Heredoc detection must not consume shift operators or quoted text.
my $shift = 4 << 2;
say $shift;
my $quoted = "first
<<NOT_A_HEREDOC";
say $quoted;
