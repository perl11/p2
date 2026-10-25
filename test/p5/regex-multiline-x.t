#!perl
# bare /.../x regex spanning several lines; the $^ and $~ format variables parse
my $w = "ab\n";
say $w =~ /\A
  a b\n
  \z/x ? "ok" : "not ok"; #=> ok
my %h = (top => $^, name => $~);
say exists $h{top} ? "ok" : "not ok"; #=> ok
my $t = 1, "ignored";
say $t; #=> 1
my ($p, $q);
$p = 5, $q = 6;
say "$p $q"; #=> 5 6
my %kh = (x => 1, y => 2);
my $kn = keys %kh;
say $kn; #=> 2
say "n ", scalar keys %kh, "!"; #=> n 2!
say "two" if keys %kh == 2; #=> two
