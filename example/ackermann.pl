sub ack {
  my ($m, $n) = @_;
  if ($m == 0) { return $n + 1; }
  if ($n == 0) { return ack($m - 1, 1); }
  return ack($m - 1, ack($m, $n - 1));
}

for my $m (0 .. 3) {
  for my $n (0 .. 3) {
    print ack($m, $n), " ";
  }
  print "\n";
}
