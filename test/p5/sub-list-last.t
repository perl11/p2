#!perl
# a comma list as the last statement of a sub / do body needs no trailing `;`
my @a = (7, 8);
my $f = sub { 1, 2 };
my @r = $f->();
print scalar(@r), "|@r\n";    #=> 2|1 2
my @d = do { 1; @a, 9 };
print "@d\n";                 #=> 7 8 9
sub g { 4, 5 }
my @g = g();
print "@g\n";                 #=> 4 5
my @x = sub { 1, 2 }->();
print "@x\n";                 #=> 1 2
my $h = sub { { a => 1 } }->();
print ref($h), "\n";          #=> HASH
