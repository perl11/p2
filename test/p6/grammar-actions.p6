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
grammar Base {
    token TOP { <word> }
    token word { <[a..z]>+ }
}
grammar Digits is Base {
    token word { <[0..9]>+ }
}
say Base.parse("abc") ?? "y" !! "n";      #=> y
say Base.parse("123") ?? "y" !! "n";      #=> n
say Digits.parse("123") ?? "y" !! "n";    #=> y
say Digits.parse("abc") ?? "y" !! "n";    #=> n
grammar Ops {
    token TOP { <num> <op> <num> }
    token num { <[0..9]>+ }
    proto token op {*}
    token op:sym<+> { '+' }
    token op:sym<-> { '-' }
}
say Ops.parse("1+2") ?? "y" !! "n";       #=> y
say Ops.parse("3-4") ?? "y" !! "n";       #=> y
say Ops.parse("3*4") ?? "y" !! "n";       #=> n
say ~Ops.parse("3-4")<op>;                #=> -
