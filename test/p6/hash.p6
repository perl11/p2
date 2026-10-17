# hashes: bareword pair keys, <k> and {k} subscripts, element assignment, anonymous hashes
my %h = (a => 1, b => 2);
say %h<a>;               #=> 1
say %h{'b'};             #=> 2
%h<c> = 3;
say %h<c>;               #=> 3
%h<a> = 9;
say %h<a>;               #=> 9
my %g;
%g<x> = 5;
say %g<x>;               #=> 5
my $r = {k => 2, "j" => 3};
say $r<k>;               #=> 2
say $r{'j'};             #=> 3
