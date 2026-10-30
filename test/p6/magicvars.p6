# compile-time context variables: $?LINE (current line), $?FILE (file name)
say "L[$?LINE]";                              #=> L[2]
say "L[$?LINE]";                              #=> L[3]
say ($?FILE.index("magicvars.p6") >= 0);      #=> True
