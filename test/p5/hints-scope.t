#!perl
# p2-only: not cross-checked (expected output pinned in hints-scope.expected)
# %^H and $^H are scoped by the enclosing block at compile time; an eval STRING compiles with the hints of its site
{
  BEGIN { $^H{foo} = "a"; $^H |= 0x20000; }
  {
    BEGIN { $^H{foo} = "b"; }
    BEGIN { print "inner ", $^H{foo}, "\n"; }
  }
  BEGIN { print "restored ", $^H{foo}, "\n"; }
  say exists $^H{foo} ? "runtime has foo" : "runtime clean";
  say $^H & 0x20000 ? "runtime hint" : "runtime no hint";
  eval q*BEGIN { print "eval ", $^H{foo}, "\n"; }*;
}
BEGIN { print exists $^H{foo} ? "leaked\n" : "outer clean\n"; }
my $s = q*a b*;
say $s;
