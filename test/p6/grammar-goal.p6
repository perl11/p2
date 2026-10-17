# goal matching: A ~ B C is A C B
grammar Paren {
    token TOP { '(' ~ ')' <inner> }
    token inner { <[a..z]>+ }
}
say Paren.parse("(abc)") ?? "y" !! "n";    #=> y
say Paren.parse("(abc") ?? "y" !! "n";     #=> n
say Paren.parse("abc)") ?? "y" !! "n";     #=> n
grammar Br {
    token TOP { <open> ~ <close> [ <[a..z]>+ ] }
    token open { '[' }
    token close { ']' }
}
say Br.parse("[xy]") ?? "y" !! "n";        #=> y
say Br.parse("[xy") ?? "y" !! "n";         #=> n
