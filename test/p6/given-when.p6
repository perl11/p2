# given/when/default
given 7 { when 7 { say "seven" }; default { say "no" } }  #=> seven
given 8 { when 7 { say "seven" }; default { say "no" } }  #=> no
