#!perl
# p2 regression test: subscripts on a parenthesized expression
my ($s, $t) = ([], {});
my $c = 1;
($c ? $s : $t)->[0] = 73;          # assignment through a ternary
say $s->[0];                       #=> 73
(!$c ? $s : $t)->{k} = 5;
say $t->{k};                       #=> 5
say(($c ? $s : $t)->[0] + 1);      #=> 74
sub f { [4, 5, 6] }
say((f())->[2]);                   #=> 6
say join ",", (1, 2, 3)[0, 2];     #=> 1,3
