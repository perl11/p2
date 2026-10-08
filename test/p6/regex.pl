use p6 { "abc" ~~ /b/ }  #=> true
use p6 { "abc" ~~ /z/ }  #=> false
use p6 { "abc" !~~ /z/ } #=> true
use p6 { "abc" ~~ m:P5/b/ }  #=> true
use p6 { "abc" ~~ m:P5/z/ }  #=> false
