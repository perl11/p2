#!perl
# p2-specific regression test: ranges in foreach, .= and += compound
# assignment, // (defined-or) with a defined-but-false left side,
# \@array references, and qualified hash elements.
foreach my $i (1..3) { say "i=$i"; }
#=> i=1
#=> i=2
#=> i=3
my $s = "a"; $s .= "b"; say $s; #=> ab
my $n = 4; $n += 2; say $n; #=> 6
my $u; my $d = $u // "dflt"; say $d; #=> dflt
my $z = 0; my $e = $z // 5; say $e; #=> 0
my @a = (1, 2); my $r = \@a; say "ref ok"; #=> ref ok
%Foo::h = (k => 4); say $Foo::h{k}; #=> 4
