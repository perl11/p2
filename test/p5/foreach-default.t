#!perl
# p2-specific regression test: foreach with the default variable $_
# (for (LIST), statement-modifier 'for'), and 'my @a;' / 'my %h;' starting
# out empty instead of undef, and a bare /pat/ matching $_.
# Expected stdout is pinned with inline markers, compared by
# test/runtests.sh -p5.
my $n = 0;
for (1, 2, 3) { $n = $n + $_; }
say $n; #=> 6
my @a = (5, 6);
my $m = 0;
for (@a) { $m = $m + $_; }
say $m; #=> 11
my $c = 0;
$c = $c + 1 for (1, 2, 3);
say $c; #=> 3
$c = $c + $_ for 1..3;
say $c; #=> 9
my @o;
push @o, $_ for (4, 5);
say @o->length; #=> 2
my %h;
$h{a} = 2;
say $h{a}; #=> 2
say $_ for (7); #=> 7
my $hits = 0;
for ("ab", "cd", "ce") { if (/c/) { $hits = $hits + 1; } }
say $hits; #=> 2
say "end"; #=> end
