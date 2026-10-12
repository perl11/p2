sub fib($n) {
  $n < 2 ?? $n !! fib($n - 1) + fib($n - 2);
}
my $N = +(@*ARGS[0] // 28);
say "fib($N) = ", fib($N);
