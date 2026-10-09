#!perl
# Native p5 regex matching through the bundled PCRE2 engine.

my $text = "cafe/path-42";
say $text =~ /path-[0-9]+/; #=> true
say $text =~ /missing/; #=> false
say $text !~ /missing/; #=> true
say $text =~ /caf./; #=> true
say $text =~ /cafe\/path/; #=> true
say $text->captures("(path)-([0-9]+)"); #=> (path-42, path, 42)
say $text->match("path-[0-9]+"); #=> true
say $text->match("missing"); #=> false
say $text->captures("(path)(z)?-([0-9]+)"); #=> (path-42, path, undef, 42)
say $text->captures("missing"); #=> ()
say "CAFE" =~ /cafe/; #=> false
say "CAFE" =~ /cafe/i; #=> true
say "a\nb" =~ /^b/m; #=> true
say "a\nb" =~ /a.b/s; #=> true
say "cafe" =~ / c a f e /x; #=> true
say "A\nB" =~ /^b/im; #=> true

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
say $sub !~ $re; #=> false
