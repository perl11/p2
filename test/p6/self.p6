# p6 classes: self, public attribute accessors, $! writes inside methods
class Pt { has $.x = 1; method get(){ self.x }; method dbl(){ self.get + self.get }; method setx($v){ $!x = $v; self.get } }
say Pt.new.get; #=> 1
say Pt.new(x => 4).dbl; #=> 8
say Pt.new.setx(9); #=> 9
say Pt.new(x => 3).x; #=> 3
class Cnt { has $.n = 0; method inc(){ $!n = $!n + 1; self }; method val(){ $.n } }
say Cnt.new.inc.inc.val; #=> 2
