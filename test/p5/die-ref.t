#!perl
# die with a reference or an object sets $@ to it; die LIST joins the items; "$@->{k}" interpolates
eval { die { code => 42 } }; print ref($@), " $@->{code}\n"; #=> HASH 42
eval { die [4, 5] }; print ref($@), " $@->[1]\n"; #=> ARRAY 5
package MyErr; sub new { bless { msg => $_[1] }, $_[0] } sub msg { $_[0]{msg} }
package main;
eval { die MyErr->new("boom") }; print ref($@), " ", $@->msg, "\n"; #=> MyErr boom
my $e = { c => 1 }; eval { die $e }; print ref($@), " $@->{c}\n"; #=> HASH 1
eval { die "a", "b", "\n" }; print $@; #=> ab
eval { die "plain\n" }; print "err: $@"; #=> err: plain
