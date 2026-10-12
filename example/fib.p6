sub fib($n) {
  $n < 2 ?? $n !! fib($n - 1) + fib($n - 2);
}
my $N = 28;
say "fib($N) = ", fib($N);
