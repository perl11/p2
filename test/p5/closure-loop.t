#!perl
# a closure captures the variable of its own loop iteration (the loop variable and a `my` of the body)
my @s; for my $n (qw(a b c)) { push @s, sub { $n } }
my $o = ""; $o .= $_->() for @s; print "$o\n"; #=> abc
my @t; for my $i (1, 2, 3) { my $j = $i * 2; push @t, sub { $j } }
$o = ""; $o .= $_->() . "," for @t; print "$o\n"; #=> 2,4,6,
my @u; my $k = 0; while ($k < 3) { my $c = $k; push @u, sub { $c++ }; $k++ }
$o = ""; $o .= $_->() . "," for @u; $o .= $_->() . "," for @u; print "$o\n"; #=> 0,1,2,1,2,3,
my $shared = 0; my $inc = sub { $shared++ }; $inc->(); $inc->();
print "$shared\n"; #=> 2
my $fact; $fact = sub { my $n = shift; $n < 2 ? 1 : $n * $fact->($n - 1) };
print $fact->(5), "\n"; #=> 120
# a global loop variable (no my) is saved and restored, closures read the global
my @g; for $gn (qw(a b)) { push @g, sub { $gn } } $o = ""; $o .= "[" . $_->() . "]" for @g; print "$o\n"; #=> [][]
our $og = "x"; my @h; for $og (qw(p q)) { push @h, sub { $og } } $o = ""; $o .= "[" . $_->() . "]" for @h; print "$o $og\n"; #=> [x][x] x
