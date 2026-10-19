#!perl
# file tests, string ranges, ... range, goto &sub, printf FH, Pkg:: words, bare unlink, empty sub
my $f = "filetest-range.tmp";
open(my $o, '>', $f) or die; print $o "abc\n"; close $o;
print((-e $f) ? "e" : "-", (-f $f) ? "f" : "-", (-d $f) ? "d" : "-", (-s $f), (-z $f) ? "z" : "-", "\n"); #=> ef-4-
print -e "$f.none" ? "y" : "n", "\n"; #=> n
print "same\n" if -e $f && -r _; #=> same
$_ = $f; print unlink, "\n"; #=> 1
print -e $f ? "still" : "gone", "\n"; #=> gone
my @a = ("a".."e"); print "@a\n"; #=> a b c d e
@a = ("08".."11"); print "@a\n"; #=> 08 09 10 11
@a = ("x".."ab"); print scalar(@a), "\n"; #=> 5
my $n = 0; for (1...4) { $n++ } print "$n\n"; #=> 4
sub g { "g(@_)" } sub h { goto &g } print h(1, 2), "\n"; #=> g(1 2)
printf STDOUT ("%s-%03d\n", "a", 7); #=> a-007
package Moo; sub new { bless {}, shift } package main;
my $t = bless {}, Moo::; print ref($t), " ", ref(Moo::->new), "\n"; #=> Moo Moo
sub empty_sub {} my $e = empty_sub; print defined($e) ? "def" : "undef", "\n"; #=> undef
