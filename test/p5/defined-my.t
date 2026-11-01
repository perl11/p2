#!perl
# defined(my $x = EXPR): a declaration with a value is an expression, true unless undef
my %h = (a => 1);
my @q = (1, 2, 3);
print defined(my $y = 5) ? "d" : "u", "\n";            #=> d
print defined(my $z = $h{zz}) ? "d" : "u", "\n";       #=> u
print defined(my $w = $h{a}) ? "d" : "u", "\n";        #=> d
my $s = 0;
while (defined(my $e = shift @q)) { $s += $e }
print "sum: $s\n";                                      #=> sum: 6
my $c = 0;
while (defined(my $k = each %h)) { $c++; last if $c > 5 }
print "keys: $c\n";                                     #=> keys: 1
