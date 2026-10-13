# p6 control flow, subs, arrays, defaults
{ sub f($n){ $n < 2 ?? $n !! f($n-1) + f($n-2) }; say f(10) } #=> 55
{ sub g($n){ if $n < 2 { return $n }; return 7 }; say g(1) + g(5) } #=> 8
{ my @a; @a.push(1); @a.push(2); say @a.elems ~ ":" ~ @a.join(",") } #=> 2:1,2
{ my @a; my $d = 1; @a[$d+1] = 9; say @a.elems ~ ":" ~ @a[2] } #=> 3:9
{ my $x = 0; for 0..3 -> $r { next if $r == 2; $x = $x + $r }; say $x } #=> 4
{ my @e; for 0 .. @e.elems - 1 -> $i { say "no" }; say "empty" } #=> empty
{ my $s = "4"; say ($s == 4 ?? "eq" !! "ne") ~ (+$s + 1) } #=> eq5
{ my $u; say ($u // 9) ~ ":" ~ (8 / 2) ~ ":" ~ (5 // 1) } #=> 9:4:5
