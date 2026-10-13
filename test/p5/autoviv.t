#!perl
# `$h{k} = v` / `$a[i] = v` as the first mention of a package hash/array creates the container,
# also when the program has a sub reading @_ (the JIT pads call sites for those, and used to
# read an uninitialised local for the missing container).
$cb{a} = sub { shift };
$S{a} = 1;
say "[$S{a}]"; #=> [1]
$A[2] = 5;
say scalar(@A), "|$A[2]"; #=> 3|5
$S{c} = 3;
say join(",", map { "$_=$S{$_}" } sort keys %S); #=> a=1,c=3
say $cb{a}->("x"); #=> x
my %h;
$h{b} = 2;
say "[$h{b}]"; #=> [2]
