# p6 classes: every attribute has a default, no constructor args
class Pd { has $.x = 1; has $.y = 4; method sum(){ $.x + $.y } }
say Pd.new.sum; #=> 5
