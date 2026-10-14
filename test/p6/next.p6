# p6 callsame, callwith, nextsame, SUPER:: and obj.Class::meth along the C3 order
class A { method m($x){ 'A' ~ $x }; method n(){ 'An' } }
class B is A { method m($x){ 'B' ~ callsame }; method n(){ 'Bn' ~ self.A::n } }
class C is B { method m($x){ 'C' ~ callwith($x ~ '!') } }
say A.new.m(1); #=> A1
say B.new.m(2); #=> BA2
say C.new.m(3); #=> CBA3!
say B.new.n; #=> BnAn

# multiple inheritance: the next method is the next class of the C3 order of the object
class X { method who(){ 'X' } }
class Y is X { method who(){ 'Y' ~ callsame } }
class Z is X { method who(){ 'Z' ~ callsame } }
class W is Y is Z { method who(){ 'W' ~ callsame } }
say W.new.who; #=> WYZX
say Y.new.who; #=> YX
class S is A { method m($x){ 'S' ~ self.A::m($x) } }
say S.new.m(4); #=> SA4
