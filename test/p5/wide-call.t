# a call with more arguments than any sub declared in the caller's file: the outgoing
# arguments beyond the sixth are written to the stack and the frame must have room for them
# (no @_ in this file: that would reserve 15 slots anyway)
sub f { 1 }
print f(1,2,3,4,5,6,7,8,9), "\n";
print f(1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20), "\n";
