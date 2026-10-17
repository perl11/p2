# ++ and --, loop (init; cond; step), loop, while
my $i = 0;
$i++; ++$i;
say $i;                       #=> 2
$i--;
say $i;                       #=> 1
my $j = $i++;
say "$j $i";                  #=> 1 2
my @o;
loop (my $n = 0; $n < 3; $n++) { @o.push($n) };
say @o.join(",");             #=> 0,1,2
my $k = 0;
loop { $k++; last if $k > 2 };
say $k;                       #=> 3
my $w = 0;
while $w < 4 { $w++ };
say $w;                       #=> 4
