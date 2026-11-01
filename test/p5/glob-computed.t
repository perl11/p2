#!perl
# *{EXPR} is the glob of a computed name, *{EXPR}{SLOT} one of its slots; \*{"X"} is a reference to it
sub hello { "hi" }
my $c = *{"main::hello"}{CODE};
print ref($c), "\n";                       #=> CODE
my $g = *{"STDOUT"};
print "$g\n";                              #=> *main::STDOUT
my $n = "hello";
my $h = *{$n};
print "$h\n";                              #=> *main::hello
print "same\n" if \*{"FOO"} eq \*{"FOO"};  #=> same
