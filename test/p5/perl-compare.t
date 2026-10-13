#!perl
# Stringification and operator probes compared against real perl: every line below was run
# with perl 5 and its output pinned as the inline marker (compared by test/runtests.sh -p5).
my $u; my $c = sub {1}; my $sv = 5; my $s = \$sv; my @a = (1,2); my %h = (k=>1); my $ar = [1,2]; my $hr = {a=>1};
print "1:", $u, "|\n"; #=> 1:|
print "2:", (1==1), "|", (1==2), "|\n"; #=> 2:1||
print "3:", 2.5, "|", 1e21, "|", 0.1+0.2, "|", 1/3, "|", -0.0, "|", 10/2, "|\n"; #=> 3:2.5|1e+21|0.3|0.333333333333333|0|5|
print "4:", ($c =~ /^CODE\(0x[0-9a-f]+\)$/ ? "ok" : "no"), "\n"; #=> 4:ok
print "5:", ($s =~ /^SCALAR\(0x[0-9a-f]+\)$/ ? "ok" : "no"), "|", ref($s), "|", ref($c), "|", ref($ar), "|", ref($hr), "|", ref(\$s), "\n"; #=> 5:ok|SCALAR|CODE|ARRAY|HASH|REF
print "6:", "@a", "|", @a, "|", scalar(@a), "|", "$a[1]", "\n"; #=> 6:1 2|12|2|2
print "7:", ($ar =~ /^ARRAY\(0x[0-9a-f]+\)$/ ? "ok" : "no"), "|", ($hr =~ /^HASH\(0x[0-9a-f]+\)$/ ? "ok" : "no"), "\n"; #=> 7:ok|ok
print "8:", join(",", 1, undef, 3), "|", "a" . undef . "b", "\n"; #=> 8:1,,3|ab
print "9:", 1_000, "|", 0x1f, "|", 0b11, "|", 017, "|", "10" + "5", "|", "3abc" + 1, "\n"; #=> 9:1000|31|3|15|15|4
print "10:", 9**20, "|", 2**63, "|", -7 % 3, "|", 7 % -3, "|", int(-3.7), "\n"; #=> 10:1.21576654590569e+19|9.22337203685478e+18|2|-2|-3
print "11:", "abc" x 2, "|", "5" . 5, "|", 1 . 1 + 1, "\n"; #=> 11:abcabc|55|12
print "12:", "a" lt "b", "|", "b" cmp "a", "|", 2 <=> 10, "|", "2" cmp "10", "\n"; #=> 12:1|1|-1|1
print "13:", !1, "|", !0, "|", !!1, "|", defined($u) ? "d" : "u", "\n"; #=> 13:|1|1|u
print "14:", sprintf("%s|%d|%.2f|%5s|%-5s|%03d", "x", 3.9, 2.345, "ab", "ab", 7), "\n"; #=> 14:x|3|2.35|   ab|ab   |007
print "15:", lc("AB"), uc("cd"), ucfirst("xy"), length("hello"), "|", substr("abcdef", 2, 3), "|", index("hello","l"), "\n"; #=> 15:abCDXy5|cde|2
print "16:", join("|", reverse(1,2,3)), "|", scalar(reverse("abc")), "\n"; #=> 16:3|2|1|cba
print "17:", join("|", sort { $a <=> $b } (10, 9, 100, 1)), "|", join("|", sort (10, 9, 100, 1)), "\n"; #=> 17:1|9|10|100|1|10|100|9
print "18:", join("|", map { $_ * 2 } grep { $_ % 2 } 1..6), "\n"; #=> 18:2|6|10
print "19:", join("|", keys %h), "|", join("|", values %h), "|", exists $h{k} ? "e" : "n", "\n"; #=> 19:k|1|e
print "20:", join("|", split(/,/, "a,b,,c,,")), "|", scalar(my @x = split(/,/, "a,b")), "\n"; #=> 20:a|b||c|2
my $t0 = time; my $dt = time - $t0; my @ix = (6, 8, 10); print "17:", ($dt < 5 ? "ok" : "bad"), "|", $ix["1foo"], "|", $ix["2"], "\n"; #=> 17:ok|8|10
sub bare { return 7 } my $bv = bare; my $bs = substr(sqrt(2), 0, 5) . index(12345, 34); print "18:$bv|$bs\n"; #=> 18:7|1.4142
${^MYVAR} = 3; { local ${^MYVAR} = 5; print "19:", ${^MYVAR}; } print "|", ${^MYVAR}, "\n"; #=> 19:5|3
