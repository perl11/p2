#!perl
# numeric global variable names: @4, $4[0], and $#{N} as the symbol table
@4 = (1, 2);
print $4[0], "|", $4[1], "\n";              #=> 1|2
@10 = (9);
print $10[0], "\n";                         #=> 9
$#{4} = 7;
print $#{4}, "\n";                          #=> 7
print $#{[]}, "\n";                         #=> -1
