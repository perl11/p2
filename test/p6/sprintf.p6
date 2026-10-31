# p2-only: zprintf (roast name) is the same native as sprintf (current Raku name).
# The body uses sprintf so raku can cross-check it; the zprintf alias is verified
# separately. Integer specifiers are bigint-aware and apply flags/width/precision;
# width cases are marker-wrapped so the padding is unambiguous.
say sprintf("%d", 42);                         #=> 42
say sprintf("%d", -314);                       #=> -314
say sprintf("%i", 314);                        #=> 314
say "<" ~ sprintf("%8d", 42) ~ ">";            #=> <      42>
say "<" ~ sprintf("%08d", 42) ~ ">";           #=> <00000042>
say "<" ~ sprintf("%-8d|", 42) ~ ">";          #=> <42      |>
say "<" ~ sprintf("%+8d", 42) ~ ">";           #=> <     +42>
say "<" ~ sprintf("% 8d", 42) ~ ">";           #=> <      42>
say "<" ~ sprintf("%8.2d", 42) ~ ">";          #=> <      42>
say "<" ~ sprintf("%8.2d", 5) ~ ">";           #=> <      05>
say "<" ~ sprintf("%.0d", 0) ~ ";";            #=> <;
say "<" ~ sprintf("%8d", -314) ~ ">";          #=> <    -314>
say sprintf("%x", 255);                        #=> ff
say sprintf("%X", 255);                        #=> FF
say sprintf("%o", 8);                          #=> 10
say sprintf("%u", 42);                         #=> 42
say sprintf("%s", "hello");                    #=> hello
say "<" ~ sprintf("%5s", "hi") ~ ">";          #=> <   hi>
say sprintf("%c", 65);                         #=> A
say sprintf("%f", 3.5);                        #=> 3.500000
say sprintf("a%d b%d c%d", 1, 2, 3);           #=> a1 b2 c3
say sprintf("%d", 453973694165307953197296969697410619233826); #=> 453973694165307953197296969697410619233826
say sprintf("%d", 42 ** 20);                   #=> 291733167875766667063796853374976
say sprintf("%08.2d", 453973694165307953197296969697410619233826); #=> 453973694165307953197296969697410619233826
say sprintf("%04x", 255);                      #=> 00ff
