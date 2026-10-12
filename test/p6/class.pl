use p6 { class Pt { has $.x = 1; has $.y; method sum(){ $.x + $.y } }; say Pt.new(x => 5, y => 2).sum }  #=> 7
use p6 { class Pt { has $.x = 1; has $.y = 4; method sum(){ $.x + $.y } }; say Pt.new.sum }  #=> 5
use p6 { class Pt { has $.x = 1; has $.y; method add($n){ $.x + $n } }; say Pt.new(y => 2).add(10) }  #=> 11
use p6 { class Pt { has $.x = 1; has $.y; method sum(){ $.x + $.y } }; say Pt.new(y => 4).sum }  #=> 5
