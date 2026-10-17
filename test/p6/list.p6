# list methods with blocks, the topic of a bare block
my @a = (3,1,2,3);
say @a.map({ $_ * 2 }).join(",");         #=> 6,2,4,6
say @a.grep({ $_ > 1 }).elems;            #=> 3
say @a.sum;                               #=> 9
say @a.max;                               #=> 3
say @a.min;                               #=> 1
say @a.unique.join("-");                  #=> 3-1-2
say (1,(2,3)).flat.elems;                 #=> 3
say (1,2,3).map(-> $x { $x + 1 }).join(" ");   #=> 2 3 4
my $f = { $_ * 2 };
say $f(4);                                #=> 8
say @a[1..2].join(",");                   #=> 1,2
say @a[0,2].join(",");                    #=> 3,2
say @a[*-1];                              #=> 3
my %h = (a => 1, b => 2, c => 3);
say %h<a b>.join(",");                    #=> 1,2
say %h{"a","c"}.join(",");                #=> 1,3
say @a.grep(* > 1).join(",");             #=> 3,2,3
say @a.map(* * 2).join(",");              #=> 6,2,4,6
say @a.map(*.Str).join("-");              #=> 3-1-2-3
say "7".Int + 1;                          #=> 8
say 5.Str;                                #=> 5
