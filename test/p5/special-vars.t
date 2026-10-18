#!perl
# punctuation variables are globals: $\ $, (print), $@ assigned by hand
{ local $, = "-"; local $\ = "!\n"; print "a", "b", "c"; } #=> a-b-c!
{ local $\ = "|"; print "x"; print STDOUT "y"; } print "\n"; #=> x|y|
$, = ":"; my @l = (1, 2, 3); print @l; print "\n"; #=> 1:2:3
$, = undef; print "plain", "\n"; #=> plain
$\ = "X"; print "p"; $\ = undef; print "\n"; #=> pX
eval { die "boom\n" }; print "err=$@"; $@ = "set"; print "[$@]\n"; #=> err=boom
#=> [set]
