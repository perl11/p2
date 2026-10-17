# p6 grammar match objects: the match is a table, a subrule is stored under its name
# (a list when it matched several times), ~ is the matched text
grammar Calc {
    token TOP { <num> [ '+' <num> ]* }
    token num { <[0..9]>+ }
}
my $m = Calc.parse("1+22+3");
say ~$m;                 #=> 1+22+3
say $m<num>.elems;       #=> 3
say ~$m<num>[1];         #=> 22
say ~$m<num>[2];         #=> 3
say Calc.parse("x") ?? "match" !! "no match"; #=> no match
say $m.from;             #=> 0
say $m.to;               #=> 6
say $m<num>[1].from;     #=> 2
say $m<num>[1].to;       #=> 4
