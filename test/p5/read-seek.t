#!perl
# read(FH, $buf, LEN, OFFSET), seek, tell, &Pkg::name
my $f = "/tmp/p2-read-seek-$$.txt";
open(A, "+>$f");
print A "_abc";
seek(A, 0, 0);
my $b = "xy";
my $n = read(A, $b, 1, 2);
say $n;                                   #=> 1
say $b;                                   #=> xy_
say tell(A);                              #=> 1
$n = read(A, $b, 10);
say "$n $b";                              #=> 3 abc
seek(A, -2, 2);
read(A, $b, 5);
say $b;                                   #=> bc
close(A);
unlink $f;
sub E::X { 9 }
say 9 == &E::X ? "call" : "no";           #=> call
