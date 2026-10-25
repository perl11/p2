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
