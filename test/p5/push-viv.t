#!perl
# push @$undef springs to life as an array; push @a, @a only adds the old elements
my $u;
push @$u, 1, 2, 3;
say join(":", @$u);                    #=> 1:2:3
my @a = (1, 2, 3);
push @a, @a;
say join(":", @a);                     #=> 1:2:3:1:2:3
my $r = [5];
push @$r, 6;
say "@$r";                             #=> 5 6
