# reduce meta-op, try and $!, repeat loops
say [+] 1,2,3;                   #=> 6
say [*] 1..5;                    #=> 120
say [~] "a","b";                 #=> ab
say [<] 1,2,3;                   #=> True
say [<=] 3,2;                    #=> False
say [min] 3,1,2;                 #=> 1
say [**] 2,3,2;                  #=> 64
say try { 5 };                   #=> 5
try { die "x" };
say $!.chars > 0 ?? "err" !! "none"; #=> err
my $i = 0;
repeat { $i++ } while $i < 2;
say $i;                          #=> 2
repeat { $i-- } until $i < 0;
say $i;                          #=> -1
