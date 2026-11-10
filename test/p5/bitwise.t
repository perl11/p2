#!perl
# String bitwise ops: & | ^ on two strings are byte-wise, not numeric.
# & uses the MINIMUM operand length, | and ^ the MAXIMUM, the shorter
# operand NUL-padded (a perl quirk). A value already used as a number,
# or a mixed string/number operand, falls back to the numeric path.
# Results carrying bytes 0x80-0xbf hit a separate pre-existing p2
# UTF-8 string-length bug (see AGENTS.md), so the operands here keep
# their results below 0x80; the values are hex-encoded for the markers.
sub hx { my $s = shift; my $h = "";
  $h .= sprintf("%02x", ord(substr($s, $_, 1))) for 0 .. length($s) - 1;
  return $h; }
print hx("abc" & "xyz"), "\n";        #=> 606062
print hx("123" & "456"), "\n";        #=> 303032
print hx("ab" | "cd"), "\n";          #=> 6366
print hx("ab" ^ "cd"), "\n";          #=> 0206
print hx("abc" & "x"), " ", length("abc" & "x"), "\n";      #=> 60 1
print hx("ab" | "xyz"), " ", length("ab" | "xyz"), "\n";    #=> 797b7a 3
print hx("a" ^ "xyz"), " ", length("a" ^ "xyz"), "\n";      #=> 19797a 3
# numeric path is unchanged (integer ops, mixed, and ~)
print 7 & 11, " ", 7 | 11, " ", 7 ^ 11, "\n";  #=> 3 15 12
print "7" & 11, "\n";                       #=> 3
print ~5, "\n";                             #=> 18446744073709551610
