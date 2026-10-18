#!perl
# $/ (readline) and $" (interpolated lists)
my $f = "/tmp/p5-rs-ls.$$";
open(F, ">$f"); print F "a\nb\n\n\n\nc\nd"; close F;
open(F, $f); { local $/ = undef; my $x = <F>; print length($x), "\n"; } close F; #=> 10
open(F, $f); { local $/ = ""; while (my $p = <F>) { print length($p), ","; } print "\n"; } close F; #=> 5,3,
open(F, $f); { local $/ = "b"; my $p = <F>; print length($p), "\n"; my $q = <F>; print length($q), "\n"; } close F; #=> 3
#=> 7
open(F, $f); my $n = 0; while (<F>) { $n++ } print "$n\n"; close F; #=> 7
unlink $f;
my @a = (1, 2, 3);
{ local $" = "-"; print "@a\n"; } #=> 1-2-3
print "@a\n"; #=> 1 2 3
$" = ","; print "@a[0,1]\n"; #=> 1,2
