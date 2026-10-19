# given/when/default
for 1,5,"x",9 {
  when 1 { say "one" }          #=> one
  when 5 { say "five" }         #=> five
  when "x" { say "ex" }         #=> ex
  default { say "d $_" }        #=> d 9
}
given 7 { when 7 { say "seven" }; default { say "no" } }  #=> seven
given 8 { when 7 { say "seven" }; default { say "no" } }  #=> no
