#!perl
# a // b evaluates a once: `shift // 7` must not shift twice
my $u; my $z = 0; my %h = (a => 0); my @q = (5, undef, 0);
print "1:", (undef // 2), "\n";       #=> 1:2
print "2:", ($u // 3), "\n";          #=> 2:3
print "3:", ($z // 4), "\n";          #=> 3:0
print "4:", ($h{a} // 5), ($h{b} // 6), "\n";   #=> 4:06
my $x; $x //= 7; $x //= 8;
print "5:$x\n";                       #=> 5:7
my @r = map { $_ // "u" } @q;
print "6:@r\n";                       #=> 6:5 u 0
sub f { print "call;"; return 9 }
print "7:", (f() // 1), "\n";         #=> call;7:9
sub g { return }
print "8:", (g() // 1), "\n";         #=> 8:1
my $cnt = 0;
sub inc { $cnt++; return undef }
my $v = inc() // inc() // 5;
print "9:$v|$cnt\n";                  #=> 9:5|2
sub t { return shift // 7 }
print "10:", join(",", t(), t(0), t(5)), "\n";    #=> 10:7,0,5
@ARGV = (undef, 0, 3);
print "11:", join(",", (shift // 7), (shift // 7), (shift // 7)), "\n";   #=> 11:7,0,3
