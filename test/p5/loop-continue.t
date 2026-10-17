#!perl
# p2 regression test: continue blocks, last/next in a bare block, lower case labels
my $i = 0;
my @o;
while ($i < 3) { $i++; next if $i == 2; push @o, "b$i" } continue { push @o, "c$i" }
say "@o";                       #=> b1 c1 c2 b3 c3
@o = ();
for my $j (1, 2) { push @o, "i$j" } continue { push @o, "c$j" }
say "@o";                       #=> i1 c1 i2 c2
@o = ();
{ push @o, "in"; last; push @o, "no" }
push @o, "after";
say "@o";                       #=> in after
@o = ();
for (1, 2) { { last if $_ == 1; push @o, "in$_" } push @o, "o$_" }
say "@o";                       #=> o1 in2 o2
local $^W = 1;
say $^W;                        #=> 1
