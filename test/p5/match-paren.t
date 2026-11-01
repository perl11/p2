#!perl
# a parenthesized subject or right side of a match: (EXPR) =~ /re/, () = (EXPR) =~ /re/, my (..) = (EXPR) =~ /re/
my ($a) = (my $str = "abcdef") =~ /(...)/;
print "$a|$str\n";                           #=> abc|abcdef
my ($p, $q) = ("xy-z") =~ /(\w+)-(\w)/;
print "$p$q\n";                              #=> xyz
my $n = () = ("aXbXc") =~ /X/g;
print "$n\n";                                #=> 2
() = ("abc") =~ /(.)(.)/;
print "$1$2\n";                              #=> ab
my $s = "abc";
print "m\n" if ($s) =~ /b/;                  #=> m
print "n\n" unless ($s) =~ /z/;              #=> n
