# multi sub: dispatch on the number and the types of the arguments, in declaration order
multi sub f(Int $x) { "int $x" }
multi sub f(Str $x) { "str $x" }
multi sub f($x, $y) { "two" }
multi sub f() { "none" }
say f(1);                #=> int 1
say f("a");              #=> str a
say f(1, 2);             #=> two
say f();                 #=> none
multi sub g(Array $a) { "array" }
multi sub g(Hash $h) { "hash" }
my @a = (1, 2);
say g(@a);               #=> array
say g({a => 1});         #=> hash
class A {
    has $.n = 5;
    multi method m(Int $x) { "int " ~ ($!n + $x) }
    multi method m(Str $x) { "str $x" }
    multi method m() { "none" }
}
my $o = A.new;
say $o.m(1);             #=> int 6
say $o.m("s");           #=> str s
say $o.m();              #=> none
my $calls = 0;
proto sub wrapped($x) { $calls = $calls + 1; {*} }
multi sub wrapped(Int $x) { "int" }
multi sub wrapped(Str $x) { "str" }
say wrapped(1);                  #=> int
say wrapped("a");                #=> str
say $calls;                      #=> 2
