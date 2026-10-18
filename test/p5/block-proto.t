#!perl
# sub NAME(&@): NAME BLOCK LIST calls NAME(sub BLOCK, LIST)
sub apply(&@) { my $c = shift; return join(",", map { $c->($_) } @_); }
sub once(&) { return $_[0]->() }
print apply { $_[0] * 2 } 1, 2, 3; print "\n"; #=> 2,4,6
print once { "single" }; print "\n"; #=> single
my @l = (4, 5);
print apply { $_[0] + 1 } @l; print "\n"; #=> 5,6
