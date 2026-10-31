# p6 comparison operators (numeric)
say 10 == 10 ?? "true" !! "false"; #=> true
say 10 != 5 ?? "true" !! "false"; #=> true
say 3 < 5 ?? "true" !! "false"; #=> true
say 5 > 3 ?? "true" !! "false"; #=> true
say 3 <= 3 ?? "true" !! "false"; #=> true
say 3 >= 3 ?? "true" !! "false"; #=> true
say 10 == 9 ?? "true" !! "false"; #=> false

# a list cmp a number / string compares the stringified forms ((1,2,3) is
# "1 2 3", less than 42 and "x"); two equal lists compare as Same. The tuple
# cmp method (potion_tuple_cmp) no longer fatals on a non-list argument.
say ((1,2,3) cmp 42) < 0 ?? "less" !! "other"; #=> less
say ((1,2,3) cmp "x") < 0 ?? "less" !! "other"; #=> less
say ((1,2,3) cmp (1,2,3)) == 0 ?? "same" !! "other"; #=> same
