#!perl
# p2 regression test: local @a / local %h are restored at the end of the block
our @a = (1, 2);
our %h = (a => 1);
sub show { join(",", @a) . "/" . join(",", sort keys %h) }
sub t { local @a = (9); local %h = (z => 26); show() }
say t();      #=> 9/z
say show();   #=> 1,2/a
sub u { local @a; local %h; push @a, 5; $h{q} = 1; show() }
say u();      #=> 5/q
say show();   #=> 1,2/a
{ local @a = (7, 8); say "@a"; }   #=> 7 8
say "@a";     #=> 1 2
