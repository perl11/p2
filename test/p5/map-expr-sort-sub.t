#!perl
# p2 regression test: map EXPR, LIST / grep EXPR, LIST and sort SUBNAME LIST / sort $subref LIST
my @a = map($_ * 2, 1, 2, 3);
say "@a";                                 #=> 2 4 6
my @b = map $_ + 1, (1, 2);
say "@b";                                 #=> 2 3
my @c = grep $_ > 1, (1, 2, 3);
say "@c";                                 #=> 2 3
my @d = grep(/b/, qw(a b c));
say "@d";                                 #=> b
sub backwards { $b <=> $a }
my @e = sort backwards (3, 1, 2);
say "@e";                                 #=> 3 2 1
my @f = sort backwards 4, 9, 5;
say "@f";                                 #=> 9 5 4
my $cmp = sub { $a <=> $b };
my @g = (9, 8, 10);
my @h = sort $cmp @g;
say "@h";                                 #=> 8 9 10
my %hh = (b => 1, a => 2);
my @k = sort keys %hh;
say "@k";                                 #=> a b
