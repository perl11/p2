# p6 smartmatch against regexes
say "abc" ~~ /b/ ?? "true" !! "false"; #=> true
say "abc" ~~ /z/ ?? "true" !! "false"; #=> false
say "abc" !~~ /z/ ?? "true" !! "false"; #=> true
say "abc" ~~ m:P5/b/ ?? "true" !! "false"; #=> true
say "abc" ~~ m:P5/z/ ?? "true" !! "false"; #=> false
