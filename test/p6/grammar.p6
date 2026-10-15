# p6 grammar: compiled at declaration time to a greg parser (Syntax object, .p2-syntax/)
use v6;
grammar Calc {
    token TOP { <num> [ '+' <num> ]* }
    token num { <[0..9]>+ }
}
say Calc.parse("1+22+3") ?? "match" !! "no match"; #=> match
say Calc.parse("1+x") ?? "match" !! "no match"; #=> no match
say Calc.parse("") ?? "match" !! "no match"; #=> no match

grammar Pair {
    rule TOP { <word> <word> }
    token word { <[a..z]>+ }
}
say Pair.parse("hello big") ?? "match" !! "no match"; #=> match
say Pair.parse("hello") ?? "match" !! "no match"; #=> no match
