#!perl
# ++ state $x / state $x ++ declare the variable once and increment it
use feature 'state';
sub a1 { ++ state $y; $y }
sub a2 { state $z ++; $z }
sub a3 { ++state $w; $w }
print a1(), a1(), a1(), "|", a2(), a2(), a2(), "|", a3(), a3(), "\n";   #=> 123|123|12
