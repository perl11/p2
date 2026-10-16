#!perl
# p2-specific regression test: `my ($a, $b) = @_;` and `my $x = shift;` at the start of a
# sub (with or without my) become plain optional parameters (see p5_myargs_stmt); every other use of @_ keeps working.
# Expected stdout is pinned with inline markers (test/runtests.sh -p5).
sub add { my ($a, $b) = @_; return $a + $b }
print add(3, 4), "\n"; #=> 7
sub method { my $self = shift; my ($x, $y) = @_; return "$self:$x:" . (defined $y ? $y : "undef") }
print method("s", 1), "\n"; #=> s:1:undef
print method("s", 1, 2), "\n"; #=> s:1:2
sub count { my ($a, $b) = @_; return scalar(@_) }
print count(1, 2, 3), "\n"; #=> 3
sub slurp { my ($a, @r) = @_; return scalar(@r) }
print slurp(1, 2, 3), "\n"; #=> 2
sub skip { my ($a, undef, $c) = @_; return "$a$c" }
print skip("x", "y", "z"), "\n"; #=> xz
sub modify { my ($a) = @_; $a = 5; return $a }
print modify(1), "\n"; #=> 5
sub later { my ($a, $b) = @_; my @c = @_; return scalar(@c) }
print later(1, 2), "\n"; #=> 2
sub missing { my ($a, $b, $c) = @_; return defined $c ? "def" : "undef" }
print missing(1, 2), "\n"; #=> undef
sub nomy { ($a, $b) = @_; return $a . $b }
print nomy("p", "q"), "\n"; #=> pq
sub nomyshift { $x = shift; return $x }
print nomyshift("s"), "\n"; #=> s
