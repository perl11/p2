# p6 classes: attribute defaults, named constructor args, methods
class Pt { has $.x = 1; has $.y; method sum(){ $.x + $.y }; method add($n){ $.x + $n } }
say Pt.new(x => 5, y => 2).sum; #=> 7
say Pt.new(y => 4).sum; #=> 5
say Pt.new(y => 2).add(10); #=> 11

# A later class must keep its own constructor defaults under the JIT.
class Pd { has $.x = 1; has $.y = 4; method sum(){ $.x + $.y } }
say Pd.new.sum; #=> 5
