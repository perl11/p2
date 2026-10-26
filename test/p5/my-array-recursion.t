# a `my @a;` in a recursive sub is a fresh array per call, also when it is first
# mentioned by an element assignment (it was vivified once, at program level)
sub t {
  my $d = shift;
  my @diag;
  my %seen;
  $diag[1] = 1 if $d == 1;
  $seen{$d} = 1 if $d == 1;
  t($d + 1) if $d < 1;
  print "d$d: ", join(",", map { $_ ? 1 : 0 } @diag[0..2]), " ", join(",", sort keys %seen), "\n";
}
t(0);
