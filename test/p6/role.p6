# p6 multiple inheritance (C3 order) and roles
class A { has $.a = 1; method who(){ 1 }; method fa(){ 10 } }
class B { has $.b = 2; method who(){ 2 }; method fb(){ 20 } }
class C is A is B { has $.c = 3; method sum(){ $.a + $.b + $.c } }
say C.new.who; #=> 1
say C.new.fa; #=> 10
say C.new.fb; #=> 20
say C.new.sum; #=> 6
say C.new(b => 5).sum; #=> 9

# diamond: C3 puts D before L and R, and Base last
class Base { has $.z = 100; method id(){ 'base' }; method zz(){ $.z } }
class L is Base { method id(){ 'l' } }
class R is Base { method id(){ 'r' }; method only_r(){ 7 } }
class D is L is R { }
say D.new.id; #=> l
say D.new.only_r; #=> 7
say D.new.zz; #=> 100

role Greet { has $.greeting = 'hi'; method greet(){ $.greeting }; method both(){ 1 } }
role Count { has $.n = 3; method count(){ $.n }; method cboth(){ 2 } }
class P does Greet does Count { has $.own = 9; method own_val(){ $.own } }
say P.new.greet; #=> hi
say P.new.count; #=> 3
say P.new.both; #=> 1
say P.new.cboth; #=> 2
say P.new.own_val; #=> 9
say P.new(n => 8).count; #=> 8

# an own method beats the role method, a role method beats an inherited one
class Q { method greet(){ 'q' }; method sing(){ 's' } }
class Q2 is Q does Greet { method both(){ 'own' } }
say Q2.new.greet; #=> hi
say Q2.new.both; #=> own
say Q2.new.sing; #=> s
