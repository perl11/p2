#!perl
# hash copy and flatten, q with other delimiters, ->[i] on a constructor, scalar(LIST), forward call with @_
my %a = (a => 1);
my %b = %a;
$b{c} = 2;
say join("|", map { "$_=$b{$_}" } sort keys %b);   #=> a=1|c=2
say join(",", sort keys %a);                       #=> a
my $h = { x => 7 };
my %c = (%$h, y => 8);
say join("|", map { "$_=$c{$_}" } sort keys %c);   #=> x=7|y=8
say join("|", %$h);                                #=> x|7
my $n = { p => { q => 5 }, l => [7, 8] };
say join(",", sort keys %$n);                      #=> l,p
say q,abc,;                                        #=> abc
say [1, 2]->[1];                                   #=> 2
say { k => 'v' }->{k};                             #=> v
sub notdef { undef }
say defined(scalar(42, notdef())) ? "def" : "undef"; #=> undef
sub outer { return inner(@_)->[0] }
sub inner { return [5] unless $_[0]; my $u = outer(0); return [$u] }
say outer(1);                                      #=> 5
my ($new, $val, $str) = (0, 5, 2);
($new = $val) |= $str;
say $new;                                          #=> 7
my $cs;
($cs = "a") .= "b";
say $cs;                                           #=> ab
say(($new = 4) | 1);                               #=> 5
1, say "comma";                                    #=> comma
