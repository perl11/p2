# user defined operators: sub infix:<..>, prefix:<..>, postfix:<..>
sub infix:<plus>($a, $b) { $a + $b };
sub prefix:<neg>($a) { 0 - $a };
sub postfix:<!>($n) { $n * 2 };
say 1 plus 2;                 #=> 3
say neg 5;                    #=> -5
say 4!;                       #=> 8
say 1 plus 2 plus 3;          #=> 6
