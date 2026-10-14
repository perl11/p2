# p6 classes: submethod BUILD and non-constant attribute defaults
class K { has $.y; submethod BUILD(:$y){ $!y = $y * 2 }; method get(){ $.y } }
say K.new(y => 5).get; #=> 10
class L { has $.v = 3; has $.w = 3 + 4; method w2(){ $.w } }
say L.new.w2; #=> 7
say L.new(w => 10).w2; #=> 10
say L.new(v => 1).w2; #=> 7
class M is L { has $.u = 1 + 1; method sum(){ $.w + $.u } }
say M.new.sum; #=> 9
say M.new(w => 1, u => 2).sum; #=> 3
