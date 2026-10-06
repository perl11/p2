#!perl
# p2-specific regression test for p5 for/foreach loop desugaring
# (literal list, array variable, qw() list, nested loops)

for my $i (1, 2, 3) {
    say $i;
}

foreach my $x (qw(a b c)) {
    say $x;
}

my @nums = (10, 20, 30);
for my $n (@nums) {
    say $n;
}

for my $i (1, 2) {
    for my $j (10, 20) {
        say $i * 100 + $j;
    }
}
