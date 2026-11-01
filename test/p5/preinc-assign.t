#!perl
# ++($x = EXPR): the assignment runs, then the variable is incremented
my $foo;
print ++($foo = "99"), "\n";          #=> 100
print ++($foo = 5), "|$foo\n";        #=> 6|6
my $bar = ++($foo = 41);
print "$bar|$foo\n";                  #=> 42|42
sub cmp3 { return join("|", @_) }
print cmp3(++($foo = 9), "eq", 10), "\n";   #=> 10|eq|10
