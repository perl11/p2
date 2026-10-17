# named parameters, also in multi subs and methods
sub f($a, :$b) { say "a=$a b=" ~ ($b // "none") }
f(1, :b(2));                         #=> a=1 b=2
f(1);                                #=> a=1 b=none
f(1, b => 3);                        #=> a=1 b=3
sub two(:$x, :$y) { say "x=" ~ ($x // "-") ~ " y=" ~ ($y // "-") }
two(:y(2), :x(1));                   #=> x=1 y=2
two(:y(2));                          #=> x=- y=2
multi sub g(Int $a, :$v) { "int " ~ ($v // "-") }
multi sub g(Str $a, :$v) { "str " ~ ($v // "-") }
say g(1, :v(5));                     #=> int 5
say g("a");                          #=> str -
say g("a", :v<x>);                   #=> str x
multi sub h(:$x) { "x " ~ ($x // "") }
multi sub h(:$y) { "y " ~ ($y // "") }
say h(:x(1));                        #=> x 1
say h(:y(2));                        #=> y 2
multi sub k(Int $n, :$sep = ",") { "n$sep$n" }
say k(3);                            #=> n,3
say k(3, :sep("-"));                 #=> n-3
class C {
    multi method m(:$a) { return "a" }
    multi method m(Int $n, :$sep = ",") { return "n$sep$n" }
}
my $c = C.new;
say $c.m(:a(1));                     #=> a
say $c.m(4, :sep("+"));              #=> n+4
