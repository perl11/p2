#!perl
# \x \x{..} \0 \033 \e \a \cX escapes, 1. 0X/0B literals,
# () as undef, scalar(@never_assigned), int == double, "$0 $. $$" interpolation
print join(",", map { ord } split //, "\x41\x{263a}\e\a\033\101\cA\c?\x4"), "\n"; #=> 65,9786,27,7,27,65,1,127,4
print length("\x{100}"), "\n"; #=> 1
print 0Xabc, " ", 0B101, " ", 1. + 1, " ", 100000. , "\n"; #=> 2748 5 2 100000
my $big = "1e3"; print(($big + 1 == 1001) ? "eq" : "ne", "\n"); #=> eq
print "n=", (not()), "\n"; #=> n=1
print "len=", scalar(@never), "\n"; #=> len=0
my $x = 'banana'; $x =~ /.a/g; print pos($x); $x =~ /.z/gc; print pos($x); $x =~ /.a/g; print pos($x), "\n"; #=> 224
print "$0" eq $0 ? "ok" : "no", "\n"; #=> ok
open(my $f, '<', $0) or die; my $l = <$f>; $l = <$f>; print "line $.\n"; #=> line 2
my $pid = "$$"; print "pid ok\n" if $pid == $$; #=> pid ok
