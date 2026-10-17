#!perl
# p2 regression test: arrays in scalar context (count), more than 12 arguments, &name; shares @_
my @a = (1, 2, 3);
my $n = @a;
say $n;                                #=> 3
say @a + 1;                            #=> 4
say "n:" . @a;                         #=> n:3
say "eq" if @a == 3;                   #=> eq
say "eq2" if 3 == @a;                  #=> eq2
my $t = @a * 2;
say $t;                                #=> 6
sub cnt { my $c = @_; return $c }
say cnt(7, 8);                         #=> 2
sub all { return scalar(@_) . ":" . join(",", @_) }
say all(1 .. 15);                      #=> 15:1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
my @big = (1 .. 30);
my $r = all(@big);
say substr($r, 0, 3);                  #=> 30:
say substr($r, -5);                    #=> 29,30
say all(1, 2, 3);                      #=> 3:1,2,3
sub g { "g: @_" }
sub f { &g }
say f(1, 2, 3);                        #=> g: 1 2 3
