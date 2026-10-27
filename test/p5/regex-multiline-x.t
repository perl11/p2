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
$^H{foo} = 7;
say exists $^H{foo} ? "e" . $^H{foo} : "-"; #=> e7
delete $^H{foo};
say exists $^H{foo} ? "e" : "gone"; #=> gone
sub lc_say { say "last comma" }
{ lc_say(), } #=> last comma
my @pl = (print(""), 5);
say "@pl"; #=> 1 5
my $hd = <<"    --";
spaced $p
    --
print $hd; #=> spaced 5
my @rx = (1, 2)x2;
say "@rx"; #=> 1 2 1 2
say "ab"x3; #=> ababab
{ package QmA; sub hi { "QmA::hi" } package QmB; our @ISA = ('QmA'); sub hi { "QmB::hi" } }
my $qo = bless {}, 'QmB';
say $qo->QmA::hi(), " ", $qo->hi(); #=> QmA::hi QmB::hi
my @ta = (1, 2, 3); my @tb = (7, 8);
my $tn = 1 ? @ta : @tb;
my @tl = 0 ? @ta : @tb;
say "$tn @tl"; #=> 3 7 8
say join(",", map { ord(chr($_)) == $_ ? "ok" : "bad" } (0x10FFFF, 0x110000, 0x1FFFFF, 0x200000, 0x3FFFFFF, 0x4000000)); #=> ok,ok,ok,ok,ok,ok
my ($ca, $cb) = (1, 2);
($ca, $cb) = ($cb, $ca) = ($ca, $cb);
say "$ca$cb"; #=> 12
say join(",", qw<a b>, qw[c], qw{d}); #=> a,b,c,d
my %mh; $mh{'a', 'b'} = 5;
say join("|", map { join(".", split /\034/, $_) } keys %mh), " ", $mh{'a', 'b'}; #=> a.b 5
my %wh = (k => 1); my ($wk, $wv);
for (; ($wk, $wv) = each %wh;) { say "$wk$wv"; } #=> k1
while (($wk, $wv) = each %wh) { say "w$wk"; last } #=> wk
open(NOF, "</nonexistent/x") or say $!{ENOENT} ? "enoent" : "other"; #=> enoent
my @gl = <test/p5/regex-multi*.t>;
say scalar(@gl) > 0 ? "glob" : "none"; #=> glob
my @rg = 1..3; my %rh = 1..4;
say "@rg ", join(",", map { "$_=$rh{$_}" } sort keys %rh); #=> 1 2 3 1=2,3=4
{ $lbl = 1; label1: say "label"; } #=> label
our $sv = 1; { local ${sv}; $sv = 3; } say ${sv}; #=> 1
say eof(NEVEROPEN) ? "eof" : "open"; #=> eof
"abc" =~ /(?<fo>.)/; my %nc = %+; say $nc{fo}; #=> a
say join(",", qw(a b c)[1, 2]); #=> b,c
my $dx = 1; say join("", $dx // 0, 1, '//'); #=> 11//
say "after"; #=> after
my @dre = ([1, 2], [3, 4]); my $drh = { a => 5 };
say ${$dre[1]}[0], ${$drh}{a}; #=> 35
say 0_2_5 + 0; #=> 21
my $wi = 0; while ($wi < 5) { $wi++ } continue { last unless $wi < 2; }
say $wi; #=> 2
my $wk = 0; WL: while ($wk < 9) { $wk++; next if $wk < 3; } continue { last WL if $wk == 4; }
say $wk; #=> 4
my @nm = ((map { $_ } (2, 3)), 7);
my %nh = ((map { $_ => 1 } (2, 3)), (4, 5), (6 => 7));
my @nc = map { $_, 1 } 8, 9;
say scalar(@nm), " ", join(",", sort keys %nh), " @nc"; #=> 3 2,3,4,6 8 1 9 1
