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
