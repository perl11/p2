#!perl
# Native p5 regex matching through the bundled PCRE2 engine.

my $text = "cafe/path-42";
say $text =~ /path-[0-9]+/; #=> 1
say $text =~ /missing/; #=> 
say $text !~ /missing/; #=> 1
say $text =~ /caf./; #=> 1
say $text =~ /cafe\/path/; #=> 1
say $text->captures("(path)-([0-9]+)"); #=> (path-42, path, 42)
say $text->match("path-[0-9]+"); #=> 1
say $text->match("missing"); #=> 
say $text->captures("(path)(z)?-([0-9]+)"); #=> (path-42, path, undef, 42)
say $text->captures("missing"); #=> ()
say "CAFE" =~ /cafe/; #=> 
say "CAFE" =~ /cafe/i; #=> 1
say "a\nb" =~ /^b/m; #=> 1
say "a\nb" =~ /a.b/s; #=> 1
say "cafe" =~ / c a f e /x; #=> 1
say "A\nB" =~ /^b/im; #=> 1

# capture variables $1.. and $&; a failed match keeps the previous ones
my $cap = "hello world 42";
if ($cap =~ /(w\w+) (\d+)/) {
  say $1; #=> world
  say $2; #=> 42
  say "got $1-$2 [$&]"; #=> got world-42 [world 42]
}
if ($cap =~ /nomatch(x)/) { say "bad"; }
say $1; #=> world

# s/// (first, /g, flags, $N in the replacement) and qr//
my $sub = "hello world";
$sub =~ s/o/0/;
say $sub; #=> hell0 world
$sub =~ s/l/L/g;
say $sub; #=> heLL0 worLd
my $swap = "a-b-c";
$swap =~ s/(\w)-(\w)/$2-$1/;
say $swap; #=> b-a-c
my $ci = "AbC";
$ci =~ s/b/x/i;
say $ci; #=> AxC
my $re = qr/W(\w+)/i;
if ($sub =~ $re) { say $1; } #=> orLd
say $sub !~ $re; #=> 

# plain $& (not only inside a string) reads the last match
if ("abc" =~ /b/) { my $m = $&; say $m; } #=> b

# more than 9 groups: $10..$12, bare and interpolated
if ("abcdefghijkl" =~ /(a)(b)(c)(d)(e)(f)(g)(h)(i)(j)(k)(l)/) { say "$9$10$11$12"; } #=> ijkl
if ("abcdefghijkl" =~ /(a)(b)(c)(d)(e)(f)(g)(h)(i)(j)(k)(l)/) { say $12; } #=> l
