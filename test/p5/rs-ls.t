#!perl
# $/ (readline) and $" (interpolated lists)
my $f = "test/p5/rs-ls.tmp";  # relative: Windows has no /tmp, run from the top directory
open(F, ">$f"); print F "a\nb\n\n\n\nc\nd"; close F;
open(F, $f); { local $/ = undef; my $x = <F>; my $c = ($x =~ tr/a-d//); print "$c\n"; } close F; #=> 4
open(F, $f); { local $/ = ""; my $k = 0; while (my $p = <F>) { $k++; } print "$k\n"; } close F; #=> 2
open(F, $f); { local $/ = "b"; my $p = <F>; print substr($p, -1), "\n"; my $q = <F>; print substr($q, 0, 1) eq "\r" || substr($q, 0, 1) eq "\n" ? "nl\n" : "other\n"; } close F; #=> b
#=> nl
open(F, $f); my $n = 0; while (<F>) { $n++ } print "$n\n"; close F; #=> 7
unlink $f;
my @a = (1, 2, 3);
{ local $" = "-"; print "@a\n"; } #=> 1-2-3
print "@a\n"; #=> 1 2 3
$" = ","; print "@a[0,1]\n"; #=> 1,2
