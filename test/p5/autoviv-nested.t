#!perl
# a store through undef containers makes them: $r->{a}, $q->[2], $h{a}{b}, $a[1][2], mixed chains,
# also for ++, op=, push @{$h{k}} and list assignment targets; "@{$ref}" interpolates the array
my $r; $r->{a} = 1; print join(",", keys %$r), "\n"; #=> a
my $q; $q->[2] = 5; print scalar(@$q), "\n"; #=> 3
my %h; $h{a}{b} = 1; print join(",", keys %{$h{a}}), "\n"; #=> b
my @a; $a[1][2] = 3; print scalar(@{$a[1]}), "\n"; #=> 3
my $d; $d->{x}[1]{y} = 7; print $d->{x}[1]{y}, "\n"; #=> 7
my $c; $c->[0]++; print "$c->[0]\n"; #=> 1
my %n; $n{a}{b}++; $n{a}{c} += 5; $n{a}{d} .= "x"; print join(",", map {"$_=$n{a}{$_}"} sort keys %{$n{a}}), "\n"; #=> b=1,c=5,d=x
my %cnt; $cnt{$_}{n}++ for qw(a b a); print join(",", map {"$_=$cnt{$_}{n}"} sort keys %cnt), "\n"; #=> a=2,b=1
my %g; push @{$g{k}}, 1, 2; push @{$g{x}{y}}, 3; print "@{$g{k}} @{$g{x}{y}}\n"; #=> 1 2 3
my $s; ($s->{a}, $s->{b}) = (1, 2); print join(",", map {"$_=$s->{$_}"} sort keys %$s), "\n"; #=> a=1,b=2
my %e; print exists $e{a}{b} ? "y" : "n", "\n"; #=> n
my $ref = [3, 4]; print "@{$ref}|@$ref\n"; #=> 3 4|3 4
