# spectral norm of the infinite matrix A(i,j) = 1 / ((i+j)(i+j+1)/2 + i + 1),
# see example/spectral-norm.pn. spectral-norm [N], default 100
sub A {
  my $i = shift;
  my $j = shift;
  my $ij = $i + $j;
  return 1.0 / ($ij * ($ij + 1) / 2 + $i + 1);
}

sub Av {
  my ($n, $x, $y) = @_;
  for (my $i = 0; $i < $n; $i++) {
    my $a = 0;
    for (my $j = 0; $j < $n; $j++) { $a += A($i, $j) * $x->[$j]; }
    $y->[$i] = $a;
  }
}

sub Atv {
  my ($n, $x, $y) = @_;
  for (my $i = 0; $i < $n; $i++) {
    my $a = 0;
    for (my $j = 0; $j < $n; $j++) { $a += A($j, $i) * $x->[$j]; }
    $y->[$i] = $a;
  }
}

sub AtAv {
  my ($n, $x, $y, $t) = @_;
  Av($n, $x, $t);
  Atv($n, $t, $y);
}

my $n = shift || 100;
my (@u, @v, @t);
for (my $i = 0; $i < $n; $i++) { $u[$i] = 1; }
for (my $i = 0; $i < 10; $i++) {
  AtAv($n, \@u, \@v, \@t);
  AtAv($n, \@v, \@u, \@t);
}
my ($vBv, $vv) = (0, 0);
for (my $i = 0; $i < $n; $i++) {
  $vBv += $u[$i] * $v[$i];
  $vv  += $v[$i] * $v[$i];
}
printf("%0.9f\n", sqrt($vBv / $vv));
