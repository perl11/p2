#!perl
# a slice as the right side of a list assignment, `my ($x, @rest) = @$ref`, reverse %h
my %h = (a => 1, b => 2, c => 3);
my ($x, $y) = @h{qw(a c)}; print "$x$y\n"; #=> 13
my @a = (5, 6, 7);
my ($p, $q) = @a[0, 2]; print "$p$q\n"; #=> 57
my ($u, $v) = @a[1 .. 2]; print "$u$v\n"; #=> 67
my $r = \%h; my ($m, $n) = @{$r}{qw(a b)}; print "$m$n\n"; #=> 12
my ($m2, $n2) = @$r{"b", "c"}; print "$m2$n2\n"; #=> 23
my $ar = [7, 8, 9];
my ($f, @t) = @$ar; print "$f @t\n"; #=> 7 8 9
my ($g, @t2) = @{$ar}; print "$g @t2\n"; #=> 7 8 9
my ($k1, $k2) = @{$ar}[1, 2]; print "$k1$k2\n"; #=> 89
my %inv = reverse %h; print join(",", map { "$_=$inv{$_}" } sort keys %inv), "\n"; #=> 1=a,2=b,3=c
my $d = { list => [7, 8], n => { x => "y" } };
print "$$d{list}[0] ${$d}{list}[1] $$d{n}{x} @{$$d{list}}\n"; #=> 7 8 y 7 8
