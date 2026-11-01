#!perl
# ++our $x at the top level increments the package global
our $c = 5;
++our $c;
print "$c|$main::c\n";                       #=> 6|6
--our $c;
print "$c\n";                                #=> 5
