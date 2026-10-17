#!perl
# p2 regression test: the DATA handle (__DATA__/__END__), chomp, readline
my $s = "line\n";
chomp $s;
say "[$s]";                 #=> [line]
chomp($s);
say "[$s]";                 #=> [line]
my $l = <DATA>;
chomp $l;
say "1:$l";                 #=> 1:alpha
$l = <DATA>;
chomp $l;
say "2:$l";                 #=> 2:beta
say defined(<DATA>) ? "more" : "eof";   #=> eof
$_ = "topic\n"; chomp;
say "[$_]";                 #=> [topic]
my @l = ("a\n", "b\n", "c");
chomp(@l);
say join("|", @l);          #=> a|b|c
chomp(my $z = "q\n");
say "[$z]";                 #=> [q]
my $w;
chomp($w = "r\n");
say "[$w]";                 #=> [r]
__DATA__
alpha
beta
