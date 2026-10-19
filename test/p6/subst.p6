# s/// on a variable and on $_, defined()
my $s = "abcb"; $s ~~ s/b/X/;
say $s;                          #=> aXcb
my $t = "abcb"; $t ~~ s:g/b/X/;
say $t;                          #=> aXcX
my $u = "aXbXc"; $u ~~ s:g/X/-/;
say $u;                          #=> a-b-c
$_ = "hello"; s/l/L/;
say $_;                          #=> heLlo
my $r;
say defined($r);                 #=> False
say defined(5);                  #=> True
