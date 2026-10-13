# p6 comparison operators (numeric)
say 10 == 10 ?? "true" !! "false"; #=> true
say 10 != 5 ?? "true" !! "false"; #=> true
say 3 < 5 ?? "true" !! "false"; #=> true
say 5 > 3 ?? "true" !! "false"; #=> true
say 3 <= 3 ?? "true" !! "false"; #=> true
say 3 >= 3 ?? "true" !! "false"; #=> true
say 10 == 9 ?? "true" !! "false"; #=> false
