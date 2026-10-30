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
# numeric keys match coercively (raku): a word slice or a string lookup hits an
# int key, and an int lookup hits a string key
my %n = (1 => "one", 2 => "two", 3 => "three");
say %n<1 3>.join("|");   #=> one|three
say %n{"2"};             #=> two
my %m = ("1" => "a", "2" => "b");
say %m{1};               #=> a
