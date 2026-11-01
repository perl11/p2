#!perl
# tr lists understand octal and hex escapes
my $v = "abc";
my $n = ($v =~ tr/\0-\377//c);
print "n=$n\n";                                      #=> n=0
my $w = "ab\x{100}";
$n = ($w =~ tr/\0-\377//c);
print "n=$n\n";                                      #=> n=1
(my $u = $v) =~ tr/a-c/\101-\103/;
print "$u\n";                                        #=> ABC
(my $x = "hello") =~ tr/\x65/E/;
print "$x\n";                                        #=> hEllo
