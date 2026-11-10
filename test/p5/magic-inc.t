#!perl
# $s++ magic: a value of the form letters-then-digits (^[A-Za-z]*[0-9]*$,
# non-empty) is incremented as a string (perl's sv_inc), everything else
# numerically (+1 of the leading number). $s-- is always numeric. Postfix
# yields the old value, prefix the new one; + and - are never magic.
my $s = "az";           $s++; print "$s\n";  #=> ba
my $s = "z";            $s++; print "$s\n";  #=> aa
my $s = "Z";            $s++; print "$s\n";  #=> AA
my $s = "a";            $s++; print "$s\n";  #=> b
my $s = "a9";           $s++; print "$s\n";  #=> b0
my $s = "z9";           $s++; print "$s\n";  #=> aa0
my $s = "aZ9";          $s++; print "$s\n";  #=> bA0
my $s = "Zz9";          $s++; print "$s\n";  #=> AAa0
my $s = "abz";          $s++; print "$s\n";  #=> aca
my $s = "007";          $s++; print "$s\n";  #=> 008
my $s = "00";           $s++; print "$s\n";  #=> 01
my $s = "0";            $s++; print "$s\n";  #=> 1
my $s = "9";            $s++; print "$s\n";  #=> 10
my $s = "99";           $s++; print "$s\n";  #=> 100
my $s = "abc123";       $s++; print "$s\n";  #=> abc124
my $s = "99999999999999999999"; $s++; print "$s\n";  #=> 100000000000000000000
# numeric fallback: the leading number + 1
my $s = "123abc";       $s++; print "$s\n";  #=> 124
my $s = "9z";           $s++; print "$s\n";  #=> 10
my $s = "1_000";        $s++; print "$s\n";  #=> 2
my $s = "12.34.56";     $s++; print "$s\n";  #=> 13.34
my $s = ".5";           $s++; print "$s\n";  #=> 1.5
my $s = "1e3";          $s++; print "$s\n";  #=> 1001
my $s = "-1";           $s++; print "$s\n";  #=> 0
my $s = "+5";           $s++; print "$s\n";  #=> 6
my $s = " 123";         $s++; print "$s\n";  #=> 124
my $s = "a b";          $s++; print "$s\n";  #=> 1
my $s = "a.b";          $s++; print "$s\n";  #=> 1
my $s = "";             $s++; print "$s\n";  #=> 1
# plain numbers stay numeric (also --, which is never magic)
my $n = 41;             $n++; print "$n\n";  #=> 42
my $n = 0.5;            $n++; print "$n\n";  #=> 1.5
my $n = 41;             $n--; print "$n\n";  #=> 40
my $n = 9223372036854775807; $n++; print "$n\n";  #=> 9223372036854775808
my $s = "az";           $s--; print "$s\n";  #=> -1
# postfix is the old value, prefix the new one
my $s = "az"; my $t = $s++;  print "t=$t s=$s\n";  #=> t=az s=ba
my $s = "az"; $t = ++$s;     print "t=$t s=$s\n";  #=> t=ba s=ba
my $n = 5;    $t = $n++;     print "t=$t n=$n\n";  #=> t=5 n=6
my $n = 5;    $t = ++$n;     print "t=$t n=$n\n";  #=> t=6 n=6
my $x; $t = ++($x = "az");   print "t=$t x=$x\n";  #=> t=ba x=ba
my $x; $t = ++($x = 1);      print "t=$t x=$x\n";  #=> t=2 x=2
# + and - are never magic
my $x = "az" + 1;          print "$x\n";  #=> 1
my $x = "az" - 1;          print "$x\n";  #=> -1
# a while loop walking characters
my $s = "a"; my @l;
while ($s lt "e") { push @l, $s; $s++; }
print join(",", @l), "\n"; #=> a,b,c,d
