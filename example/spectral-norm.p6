# spectral norm, see example/spectral-norm.pl and example/spectral-norm.pn
# spectral-norm [N], default 100
sub A($i, $j) {
  my $ij = $i + $j;
  return 1.0 / ($ij * ($ij + 1) / 2 + $i + 1);
}

sub Av($n, @x, @y) {
  for 0 .. $n - 1 -> $i {
    my $a = 0;
    for 0 .. $n - 1 -> $j { $a = $a + A($i, $j) * @x[$j]; }
    @y[$i] = $a;
  }
}

sub Atv($n, @x, @y) {
  for 0 .. $n - 1 -> $i {
    my $a = 0;
    for 0 .. $n - 1 -> $j { $a = $a + A($j, $i) * @x[$j]; }
    @y[$i] = $a;
  }
}

sub AtAv($n, @x, @y, @t) {
  Av($n, @x, @t);
  Atv($n, @t, @y);
}

my $n = +(@*ARGS[0] // 100);
my @u; my @v; my @t;
for 0 .. $n - 1 -> $i { @u[$i] = 1; }
for 0 .. 9 -> $i {
  AtAv($n, @u, @v, @t);
  AtAv($n, @v, @u, @t);
}
my $vBv = 0; my $vv = 0;
for 0 .. $n - 1 -> $i {
  $vBv = $vBv + @u[$i] * @v[$i];
  $vv = $vv + @v[$i] * @v[$i];
}
say sprintf("%0.9f", sqrt($vBv / $vv));
