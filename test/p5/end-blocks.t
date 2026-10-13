#!perl
# END blocks run after the program, last defined first.
END { print "end1\n"; }
END { print "end2\n"; }
print "main\n"; #=> main
print "done\n"; #=> done
# the END output follows the program output:
# #=> end2
# #=> end1
