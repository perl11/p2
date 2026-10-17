#!perl
# p2 regression test: slice assignment, interpolated slices, postfix dereference
my @a = (1, 2, 3, 4);
my %h = (a => 1, b => 2, c => 3);
say "@a[0,3] @h{qw(a b)}";             #=> 1 4 1 2
@a[0,1] = (9, 8);
say "@a";                              #=> 9 8 3 4
@a[3..5] = (5, 6, 7);
say "@a";                              #=> 9 8 3 5 6 7
@h{qw(x y)} = (10, 20);
say join(",", map { "$_=$h{$_}" } sort keys %h);   #=> a=1,b=2,c=3,x=10,y=20
my $r = [5, 6, 7];
my $hr = { x => 1, y => 2 };
say join(",", $r->@*), " ", scalar($r->@*), " ", $r->$#*;   #=> 5,6,7 3 2
say join(",", sort keys $hr->%*);      #=> x,y
say join(",", $r->@[0,1]), " ", join(",", $hr->@{qw(x y)});   #=> 5,6 1,2
@$r[0,1] = (7, 8);
say "@$r";                             #=> 7 8 7
