#!perl
# tied on an array / hash element is a call argument too: an untied element is undef
sub show { print defined($_[0]) ? "def" : "undef", "|", $_[1], "\n" }
my @a = (1, 2);
my %h = (k => 1);
show(tied $a[0], 3);     #=> undef|3
show(tied $h{k}, 4);     #=> undef|4
show(tied($h{k}), 5);    #=> undef|5
my $s = 1;
show(tied $s, 6);        #=> undef|6
