use p6 { "abc" ~~ /b/ }  #=> true
use p6 { "abc" ~~ /z/ }  #=> false
use p6 { "abc" !~~ /z/ } #=> true
