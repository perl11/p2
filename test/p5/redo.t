#!perl
# redo: restart the loop body without re-testing the condition (while),
# without re-binding the loop variable or advancing the index (foreach),
# and with the step at the end (C-style for), like perl.
my $n = 0;
for my $i (1, 2) { $n++; print "i=$i n=$n\n"; redo if $n == 1; }
                                       #=> i=1 n=1
                                       #=> i=1 n=2
                                       #=> i=2 n=3
my $c = 0;
while ($c < 5) { $c++; print "c=$c "; redo if $c == 3; }
                                  #=> c=1 c=2 c=3 c=4 c=5
my $g = 0;
for my $x (1, 2, 3) { $g++; print "x=$x g=$g "; redo if $x == 2 && $g < 4; }
                                 #=> x=1 g=1 x=2 g=2 x=2 g=3 x=2 g=4 x=3 g=5
my $i;
for ($i = 0; $i < 3; $i++) { print "f$i "; redo if $i == 0 && 0; }
                               #=> f0 f1 f2
my $k = 0;
L: { $k++; print "L$k "; redo L if $k < 3; }
                            #=> L1 L2 L3
my $out = "";
OUT: for my $a (1, 2) { for my $b (1, 2) { $out .= "$a$b "; last OUT if $a == 2 && $b == 1; } }
print $out, "\n";            #=> 11 12 21
my $s = "";
for my $w (qw(p q r)) { $s .= $w; redo if $w eq "q" && !index($s, "Q"); $s .= "Q"; }
print $s, "\n";              #=> pqQr
print "done\n";              #=> done
