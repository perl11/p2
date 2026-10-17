#!perl
# p2 regression test: local $h{k} / local $a[i] restore the element (or delete a new key) at block end
our %h = (k => 1);
our @a = (1, 2);
sub t { local $h{k} = 2; $h{k} }
say t(), $h{k};                     #=> 21
sub u { local $a[0] = 9; $a[0] }
say u(), $a[0];                     #=> 91
sub w { local $h{new} = 5; exists $h{new} ? "has" : "no" }
say w(), exists $h{new} ? " still" : " gone";   #=> has gone
sub x { local $h{k}; defined $h{k} ? "def" : "undef" }
say x(), $h{k};                     #=> undef1
$_ = "outer";
sub v { local $_ = "inner"; $_ }
say v(), $_;                        #=> innerouter
