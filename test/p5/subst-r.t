#!perl
# s///r: the new string is the value and the subject is left alone (also for a constant)
my $s = "hello world";
my $t = $s =~ s/world/perl/r;
print "$s|$t\n";  #=> hello world|hello perl
my $c = "david" =~ s/david/rules/r;
print "$c\n";  #=> rules
my $n = "a1b2";
my $u = $n =~ s/(\d)/<$1>/gr;
print "$u|$n\n";  #=> a<1>b<2>|a1b2
my $e = $n =~ s/(\d)/$1*2/ger;
print "$e|$n\n";  #=> a2b4|a1b2
my @l = map { s/o/0/r } qw(foo boo);
print "@l\n";  #=> f0o b0o
(my $k = $s) =~ s/hello/bye/;
print "$k|$s\n";  #=> bye world|hello world
my $cnt = ($s =~ s/o/0/g);
print "$cnt|$s\n";  #=> 2|hell0 w0rld
