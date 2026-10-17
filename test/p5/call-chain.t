#!perl
# p2 regression test: calls and subscripts on the value of a function call
sub mk { return sub { 4 } }
sub rec { return { x => 9, l => [5, 6] } }
sub arr { return [1, 2] }
say mk()->();                      #=> 4
say rec()->{x}, rec()->{l}[1];     #=> 96
say arr()->[1] + arr()->[0];       #=> 3
my $f = sub { my $n = shift; sub { $n + shift } };
say $f->(1)->(2);                  #=> 3
