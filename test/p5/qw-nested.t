#!perl
# qw<...> with literal angle brackets inside (balanced, as in perl)
my @a = qw< < > >;
print scalar(@a), "|", join(",", @a), "\n";   #=> 2|<,>
my @b = qw< < > ( ) >;
print scalar(@b), "|", join(",", @b), "\n";   #=> 4|<,>,(,)
my @c = qw< a b >;
print scalar(@c), "|", join(" ", @c), "\n";   #=> 2|a b
