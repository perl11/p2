# sigilless parameters and default values
sub f(\x) { x + 1 };
say f(2);                     #=> 3
sub g(\a, \b) { a * b };
say g(3, 4);                  #=> 12
sub h($a, $b = 5) { $a + $b };
say h(1);                     #=> 6
say h(1, 2);                  #=> 3
sub k($a, $b = $a * 2) { $b };
say k(4);                     #=> 8
say k(4, 1);                  #=> 1
class A { method m($x, $y = 10) { $x + $y } };
my $o = A.new;
say $o.m(1);                  #=> 11
say $o.m(1, 2);               #=> 3
my $l = -> $a, $b = 3 { $a * $b };
say $l(2);                    #=> 6
say $l(2, 4);                 #=> 8
