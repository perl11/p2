#!perl
# -0x10 is one negation of a hex literal, not "-0 x 10"; && binds tighter than ||; <, > on numeric strings;
# my( without a space; qq with ' | " ~ ^ < > delimiters; NaN is unordered; float modulus beyond UV;
# \x{..} with underscores; ${^GLOBAL_PHASE}; chr beyond a byte
CHECK { print "check ${^GLOBAL_PHASE}\n"; } #=> check CHECK
INIT { print "init ${^GLOBAL_PHASE}\n"; } #=> init INIT
print -0x10, " ", -0b11, " ", -0100, " ", 2-0x10, "\n"; #=> -16 -3 -64 -14
print "a" . (1 || 0 && 0 ? "t" : "f"), "\n"; #=> at
print((("s" < "s") ? "lt" : "nl"), (("4" > "3") ? "gt" : "ng"), (("10" < "9") ? "lt" : "nl"), "\n"); #=> nlgtnl
my @a = (1.2, " 1.2");
print $a[0] == $a[1] ? "eq\n" : "ne\n"; #=> eq
sub two { my($x, $y) = @_; "$x$y" }
print two(1, 2), "\n"; #=> 12
print qq'q1 \$x', qq|q2|, qq"q3", qq~q4~, qq^q5^, qq<a<b>c>, "\n"; #=> q1 $xq2q3q4q5a<b>c
my $nan = 'NaN' + 0;
print defined($nan <=> 1) ? "def" : "undef", defined(1 <=> $nan) ? "def" : "undef", "\n"; #=> undefundef
print 13e21 % 4e21 == 1e21 ? "fmod\n" : "int\n"; #=> fmod
print "\x{6_9}\x{77_}\x{9__0}" eq "iw" . chr(9) ? "ok\n" : "nok\n"; #=> ok
print length(chr(4660)), "\n"; #=> 1
print "\x00h" eq chr(0) . "h" ? "nul\n" : "nonul\n"; #=> nul
print "run ${^GLOBAL_PHASE}\n"; #=> run RUN
my $big = eval(qq[qq\x{8000_0000}abc\x{8000_0000}]);
print "$big\n"; #=> abc
my $smile = eval(qq[qq\x{263a}x\x{263a}]);
print "$smile\n"; #=> x
