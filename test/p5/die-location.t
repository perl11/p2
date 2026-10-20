#!perl
# die without a trailing newline says " at FILE line N." (the line of the statement), "Died" for an empty die
eval { die "boom" }; print $@; #=> boom at test/p5/die-location.t line 3.
eval {
  die
};
print $@; #=> Died at test/p5/die-location.t line 5.
sub f {
  die "in f";
}
eval { f() }; print $@; #=> in f at test/p5/die-location.t line 9.
eval { die "with newline\n" }; print $@; #=> with newline
