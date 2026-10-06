#!perl
# p2-specific regression test for ||/&& carrying a full comparison on
# their RHS operand (not just a bare value), plain ||/&&, ternary,
# named-call precedence ('ok EXPR, desc' style), and 'eq'/'ne' doing a
# real string-coercing comparison (not just identity/bit comparison)
# so mixed int-vs-string operands compare correctly.

my $x = 'bar';
if ($x eq 'foo' || $x eq 'bar') {
    say "matched-or-eq";
}

if ($x eq 'a' || $x eq 'b' || $x eq 'bar') {
    say "matched-chain";
}

say 5 || 0;
say 0 || 7;
say 1 && 2;

say 1 ? "yes" : "no";
say 0 ? "yes" : "no";

say "1" eq "1";
say 1 eq "1";
say 3 > 2;
