# where clauses, proto, the most specific multi candidate first
sub small(Int $x where * < 10) { "small $x" }
say small(3);                    #=> small 3
sub even($n where { $n % 2 == 0 }) { "even" }
say even(4);                     #=> even
sub five($n where 5) { "five" }
say five(5);                     #=> five
multi sub f(Int $x) { "int" }
multi sub f(Int $x where * > 10) { "big" }
multi sub f(Int $x where * < 0) { "neg" }
multi sub f($x) { "any" }
say f(5);                        #=> int
say f(50);                       #=> big
say f(-3);                       #=> neg
say f("s");                      #=> any
my $v = 50;
say f($v);                       #=> big
proto sub g($x) {*}
multi sub g(Int $x) { "int" }
multi sub g(Str $x) { "str" }
say g(1);                        #=> int
say g("a");                      #=> str
class A {
    proto method m($x) {*}
    multi method m(Int $x) { "int" }
    multi method m(Str $x where *.chars > 2) { "long" }
    multi method m(Str $x) { "short" }
}
my $o = A.new;
say $o.m(1);                     #=> int
say $o.m("abcd");                #=> long
say $o.m("a");                   #=> short
my @a = (1,2,3,4,5);
say @a.grep(* + 1 > 3).join(",");   #=> 3,4,5
sub rng(Int $x where 1..5) { "in" }
say rng(3);                      #=> in
multi sub kind($x where 1..5) { "low" }
multi sub kind($x where /^a/) { "a-word" }
multi sub kind($x) { "other" }
say kind(3);                     #=> low
say kind(9);                     #=> other
say kind("abc");                 #=> a-word
say (5 ~~ Int) ?? "y" !! "n";    #=> y
say ("a" ~~ Int) ?? "y" !! "n";  #=> n
say (3 ~~ 1..5) ?? "y" !! "n";   #=> y
