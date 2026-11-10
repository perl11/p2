#!perl
# ($u) = f() / my ($u) = f(): a single target of a paren list assignment takes the first
# element of a list result, or the value itself when the call returns a scalar.
# Before the fix, the whole tuple went into the variable (an ARRAY(...) in print).
sub two { (4, 5) }
sub one { return "word" }
my ($a) = (1, 2);  print "a=$a\n";      #=> a=1
my ($b) = two();   print "b=$b\n";      #=> b=4
my ($c) = one();   print "c=$c\n";      #=> c=word
($d) = (9, 8);     print "d=$d\n";      #=> d=9
my @t = two();
my ($e) = @t;      print "e=$e\n";      #=> e=4
my ($f, $g) = two(); print "f=$f g=$g\n"; #=> f=4 g=5
my ($h) = "";      print "h=[]\n";      #=> h=[]
