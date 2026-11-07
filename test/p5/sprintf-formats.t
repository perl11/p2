#!perl
# sprintf: %b %B, * width and precision, %N$ argument index, %vd, more than six arguments, strings longer than 511 bytes,
# numeric prefix of strings for %d, # and precision flags
print length(sprintf("%s", "x" x 1000)), "\n"; #=> 1000
print sprintf("%d %d %d %d %d %d %d %d", 1, 2, 3, 4, 5, 6, 7, 8), "\n"; #=> 1 2 3 4 5 6 7 8
my @a = (1 .. 8); print sprintf("%d%d%d%d%d%d%d%d", @a), "\n"; #=> 12345678
printf("%b %B %#b %#x %#o\n", 10, 5, 5, 255, 8); #=> 1010 101 0b101 0xff 010
printf("%*d|%-*d|%.*f\n", 5, 42, 5, 42, 2, 3.14159); #=>    42|42   |3.14
printf("%3\$s %1\$s %2\$s\n", "a", "b", "c"); #=> c a b
printf("%vd\n", "1.22.333"); #=> 49.46.50.50.46.51.51.51
printf("%d|%i\n", "12abc", "17.9"); #=> 12|17
printf("%05.1f|%+.2d|%#.3o|%.0f\n", 3.14159, 7, 8, 2.5); #=> 003.1|+07|010|2
printf("[%*s] [%-*s]\n", 6, "ab", 6, "cd"); #=> [    ab] [cd    ]
printf("%c|%5c|%-5c|\n", 65, 66, 67); #=> A|    B|C    |
