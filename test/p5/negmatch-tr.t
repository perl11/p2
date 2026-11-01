#!perl
# `$s !~ tr///` and `$s !~ s///` are the negated operations
my $v = "abc";
my $w = "ab\x{100}";
print(($v !~ tr/\0-\377//c) ? "latin1" : "wide", "\n");   #=> latin1
print(($w !~ tr/\0-\377//c) ? "latin1" : "wide", "\n");   #=> wide
my $s = "hello";
print(($s !~ s/z/y/) ? "no subst" : "subst", "|$s\n");    #=> no subst|hello
print(($s !~ s/l/L/) ? "no subst" : "subst", "|$s\n");    #=> subst|heLlo
