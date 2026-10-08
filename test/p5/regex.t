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
