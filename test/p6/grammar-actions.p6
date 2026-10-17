# grammar actions (make / .made), $<name>, <.rule>, <!before>, subparse
grammar Calc {
    token TOP { <num> [ '+' <num> ]* }
    token num { <[0..9]>+ }
}
class Act {
    method num($/) { make +$/ }
    method TOP($/) { my $s = 0; for $<num> -> $n { $s += $n.made }; make $s }
}
my $m = Calc.parse("1+22+3", :actions(Act.new));
say $m.made;                 #=> 26
grammar G {
    token TOP { <word> <.sep> <word> }
    token sep { ',' }
    token word { <!before 'x'> <[a..z]>+ }
}
my $g = G.parse("ab,cd");
say ~$g;                     #=> ab,cd
say ~$g<word>[1];            #=> cd
say $g<sep> ?? "sep" !! "nosep";   #=> nosep
say G.parse("xb,cd") ?? "m" !! "no"; #=> no
grammar W { token TOP { <[a..z]>+ } }
say ~W.subparse("ab,cd");    #=> ab
say W.parse("ab,cd") ?? "full" !! "partial";   #=> partial
