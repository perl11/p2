# heredocs: q:to|DELIM| (no interpolation) and qq:to|DELIM| (interpolation);
# the body runs from the line after the introducer to the line that is exactly
# the delimiter; the newline ending the last content line is kept
my $s = q:to|HERE|
hello
world
HERE
;
say $s.chars;              #=> 12
say $s.index("world");     #=> 6
say $s.substr(0,5);        #=> hello
my $n = 3;
my $t = qq:to|HERE|
value is $n
HERE
;
say $t.chars;              #=> 11
say $t.index("3");         #=> 9
my $u = q:to<HERE>
abc
HERE
;
say $u.chars;              #=> 4
say $u.substr(0,3);        #=> abc
my $z = 42;
my $v = q:to|HERE|
$z stays literal
HERE
;
say $v.index('$z');        #=> 0
say $v.chars;              #=> 17
