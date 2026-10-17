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
