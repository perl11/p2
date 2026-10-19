# when/default inside a for loop
for 1,5,"x",9 {
  when 1 { say "one" }          #=> one
  when 5 { say "five" }         #=> five
  when "x" { say "ex" }         #=> ex
  default { say "d $_" }        #=> d 9
}
