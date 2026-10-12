sub ack($m, $n) {
  if $m == 0 { return $n + 1 }
  if $n == 0 { return ack($m - 1, 1) }
  return ack($m - 1, ack($m, $n - 1));
}

for 0..3 -> $m {
  for 0..6 -> $n {
    print ack($m, $n), " ";
  }
  print "\n";
}
