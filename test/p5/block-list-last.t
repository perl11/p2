#!perl
# every block (if/else/eval/do/sub) may end with a list and no `;`; a bare `{ a => 1 }` statement is still a hash
my @r = eval { 1, 2, 3 };
print "@r\n";                                #=> 1 2 3
my $x = 1;
my @s = do { if ($x) { 4, 5 } else { 6 } };
print "@s\n";                                #=> 4 5
my $h = sub { { a => 1 } }->();
print ref($h), "\n";                         #=> HASH
eval { print { *STDOUT } "x", "y" };
print "\n";                                  #=> xy
