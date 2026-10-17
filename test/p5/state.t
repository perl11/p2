#!perl
# p2 regression test: state variables keep their value between calls
use feature 'state';
sub c { state $n = 0; return ++$n }
say c(), c(), c();                      #=> 123
sub d { state $s; $s .= "x"; return $s }
say d(), d();                           #=> xxx
sub e { state @a; push @a, 1; return scalar(@a) }
say e(), e(), e();                      #=> 123
sub f { state %h; $h{$_[0]} = 1; return scalar(keys %h) }
say f("a"), f("b"), f("a");             #=> 122
my @seen;
for my $i (1 .. 3) { state $t = 10; $t++; push @seen, $t }
say "@seen";                            #=> 11 12 13
