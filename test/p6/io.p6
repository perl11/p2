# slurp, spurt, .IO, .lines, .words, .chars, unlink, and a grammar reading a file
my $f = "p6-io-test.tmp";
spurt $f, "hello\nbig world\n";
say slurp($f).chars;                 #=> 16
say $f.IO.slurp.lines.elems;         #=> 2
say "big world".words.elems;         #=> 2
$f.IO.spurt("1+22");
say slurp $f;                        #=> 1+22
grammar Calc {
    token TOP { <num> [ '+' <num> ]* }
    token num { <[0..9]>+ }
}
say Calc.parsefile($f) ?? "match" !! "no match";   #=> match
say unlink($f) ?? "gone" !! "still";               #=> gone
