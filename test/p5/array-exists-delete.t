#!perl
# p2 regression test: exists/delete on array elements, "$#$r" and "$#{$r}" interpolation
my @a = (1, 2, 3);
say exists $a[2] ? "yes" : "no";   #=> yes
say exists $a[3] ? "yes" : "no";   #=> no
say exists $a[-1] ? "yes" : "no";  #=> yes
my $old = delete $a[1];
say $old;                          #=> 2
say defined($a[1]) ? "def" : "undef";   #=> undef
say scalar(@a);                    #=> 3
delete $a[2];
say scalar(@a);                    #=> 1
my $r = \@a;
say "last $#$r $#{$r} $#a";        #=> last 0 0 0
my %h = (k => 1);
say exists $h{k} ? "h" : "-";      #=> h
delete $h{k};
say exists $h{k} ? "h" : "-";      #=> -
