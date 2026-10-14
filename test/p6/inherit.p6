# p6 classes: single inheritance with `is`, attributes and methods are inherited
class Base { has $.x = 1; method getx(){ $.x }; method who(){ 10 } }
class Child is Base { has $.y = 2; method gety(){ $.y }; method who(){ 20 } }
class Grand is Child { has $.z = 3; method sum(){ $.x + $.y + $.z } }
say Child.new(x => 5).getx; #=> 5
say Child.new(y => 7).getx; #=> 1
say Child.new(y => 7).gety; #=> 7
say Child.new.who; #=> 20
say Base.new.who; #=> 10
say Grand.new.sum; #=> 6
say Grand.new(x => 10, z => 100).sum; #=> 112
say Grand.new.who; #=> 20
