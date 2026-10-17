#!perl
# p2 regression test: m// with other delimiters, m//g in list and scalar context with pos, tr///,
# s/// with a count, variables and /e in the replacement, @- @+ %+
my $s = "a1b22";
say "m1" if $s =~ m{\d+};             #=> m1
say "m2" if $s =~ m!b!;               #=> m2
say "m3" unless $s =~ m#z#;           #=> m3
my @n = $s =~ /(\d+)/g;
say "@n";                             #=> 1 22
my @got;
while ($s =~ /(\d+)/g) { push @got, "$1:" . pos($s); }
say "@got";                           #=> 1:2 22:5
my $c = () = "aXbXc" =~ /X/g;
say $c;                               #=> 2
my ($p, $q) = "1-2" =~ /(\d)-(\d)/;
say "$p$q";                           #=> 12
(my $t = "hello") =~ tr/a-y/b-z/;
say $t;                               #=> ifmmp
my $cnt = ($s =~ tr/0-9//);
say $cnt;                             #=> 3
say "hello" =~ tr/a-z/A-Z/r;          #=> HELLO
my $v = "aabbcc"; $v =~ tr/a-c//s;
say $v;                               #=> abc
(my $u = $s) =~ s/(\d+)/<$1>/g;
say $u;                               #=> a<1>b<22>
(my $w = $s) =~ s/(\d+)/$1*2/ge;
say $w;                               #=> a2b44
my $z = "aaa";
my $k = ($z =~ s/a/b/g);
say "$k $z";                          #=> 3 bbb
my $e = "baac"; $e =~ s/a*/-/g;
say $e;                               #=> -b--c-
my $path = "a/b"; $path =~ s{/}{::};
say $path;                            #=> a::b
if ("ab" =~ /(?<x>a)(?<y>b)/) { say "$+{x}$+{y} $-[0] $+[0] $-[2] $+[2]"; }   #=> ab 0 2 1 2
