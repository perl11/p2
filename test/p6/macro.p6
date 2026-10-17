# macros: expanded when the call is translated, the arguments are asts
use experimental :macros;
macro twice($e) { quasi { {{{$e}}} + {{{$e}}} } }
say twice(21);                 #=> 42
macro hello() { quasi { say "hello" } }
hello();                       #=> hello
macro scaled($a, $b) { quasi { {{{$a}}} * 10 + {{{$b}}} } }
say scaled(4, 2);              #=> 42
say scaled(1 + 1, 3) + 1;      #=> 24
my $n = 5;
say twice($n);                 #=> 10
