#!perl
# perl number semantics (the Potion core keeps integer division): exact integer
# quotients stay integers, else a double; % has the sign of the right operand;
# ** is a double for negative or huge exponents; doubles print with %.15g.
# Expected stdout is pinned with inline markers, compared by test/runtests.sh -p5.
say 7/2; #=> 3.5
say 6/2; #=> 3
say -7/2; #=> -3.5
say 1/3; #=> 0.333333333333333
say 7%3; #=> 1
say -7%3; #=> 2
say 7%-3; #=> -2
say 2**-1; #=> 0.5
say 2**10; #=> 1024
say 2**64; #=> 1.84467440737096e+19
say 9**0.5; #=> 3
say 1e3/7; #=> 142.857142857143
say 1.5e3; #=> 1500
say 0.1+0.2; #=> 0.3
# print LIST concatenates all items (numbers are not added)
print 1, 2, "\n"; #=> 12
say "a", 3, "b"; #=> a3b
# a negative literal is one token; ** and % still follow perl precedence
say -7%3; #=> 2
say 2- -3; #=> 5
say -7**2; #=> -49
# float literals: negative, without leading digit, unary plus
say -2.5; #=> -2.5
say -.5; #=> -0.5
say .5+1; #=> 1.5
say 3 - -.5; #=> 3.5
say -1e3; #=> -1000
say +3; #=> 3
# '+' is numeric, '.' concatenates (also long chains and interpolation)
say "3"+4; #=> 7
say "3"."4"; #=> 34
say "a" . "b" . 5 . 1.5 . "c" . "d" . "e" . "f"; #=> ab51.5cdef
my @ia = (10, 20, 30);
my $ii = 1;
say "v $ia[1] $ia[$ii] $ia[0] $ia[2] end"; #=> v 20 20 10 30 end
my $cs = "q"; $cs .= "r" . 1;
say $cs; #=> qr1
# digit separators, variables interpolated into regex literals
say 10_000 + 1; #=> 10001
my $re = '\d+';
say "a12" =~ /a$re/ ? "m" : "n"; #=> m
my $cls = '\s';
say " " =~ /[$cls]/ ? "m" : "n"; #=> m
# a sub called before its definition may use subs of a required file
BEGIN { require "./test/p5/test.pl"; }
late_call(); #=> ok 1 - late
sub late_call { is(1, 1, "late"); }
# numeric builtins; a named unary operator takes one argument
say int(3.7), int(-3.7); #=> 3-3
say int "42abc", "x"; #=> 42x
say hex("ff"), " ", oct("0x1f"), " ", oct("755"), " ", hex "0x10"; #=> 255 31 493 16
say sqrt(16), exp(0), log(1), sin(0), cos(0); #=> 41001
say time > 1000 ? "t" : "f"; #=> t
