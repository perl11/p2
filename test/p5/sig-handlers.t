#!perl
# $SIG{__WARN__} and $SIG{__DIE__}: handlers get the message; the handler is off while it runs;
# a __DIE__ handler may die with another message; local restores the previous handler
my $log = "";
$SIG{__WARN__} = sub { $log .= "W:$_[0]" };
warn "w1\n";
{ local $SIG{__WARN__} = sub { $log .= "inner:$_[0]" }; warn "w2\n"; }
warn "w3\n";
print $log; #=> W:w1
#=> inner:w2
#=> W:w3
$log = "";
$SIG{__DIE__} = sub { $log .= "D:$_[0]" };
eval { die "x\n" };
print $log, "after: $@"; #=> D:x
#=> after: x
$SIG{__DIE__} = sub { die "wrapped: $_[0]" };
eval { die "y\n" };
print "got: $@"; #=> got: wrapped: y
$SIG{__DIE__} = 'DEFAULT';
eval { die "z\n" };
print "got: $@"; #=> got: z
{ local ($^W, $SIG{__WARN__}) = (1, sub { print "L:@_" }); warn "hi\n"; } #=> L:hi
