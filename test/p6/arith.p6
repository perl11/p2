# p6 arithmetic: check precedence and basic ops
use v6;
say (1 + 2, 10 - 3, 3 * 4, 10 / 2, 2 ** 3, 10 % 3, 1 + 2 * 3 ).join(" "); #=> 3 7 12 5 8 1 7
