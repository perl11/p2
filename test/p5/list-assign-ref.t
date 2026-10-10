#!perl
# my (LIST) = @$ref / @{$ref}: elements read as $ref->[i]; a long single-quoted
# string in a VALUE node must not be mistaken for a lick AST node.
# Expected stdout is pinned with inline markers, compared by test/runtests.sh -p5.
my $r = [5, 6];
my ($c, $d) = @$r;
say $d; #=> 6
my ($e, $f, $g) = @{$r};
say $f; #=> 6
say $g; #=> undef
my ($x) = ($r);
say $x->length; #=> 2
sub is { return $_[1] }
say is(join(' ', @$r), 'x y z', 'void unshift singleton list (ref)'); #=> x y z
