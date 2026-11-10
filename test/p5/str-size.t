#!perl
# p2-only
# "size" is the number of bytes in a string; "length" is the number of UTF-8
# characters (see chr-length.t). A high codepoint is 2-3 bytes but 1 character.
print chr(0x9e)->size, " ", chr(0x100)->size, " ", chr(0x20ac)->size, "\n";  #=> 2 2 3
print "abc"->size, " ", "\x9e\x9d"->size, "\n";                              #=> 3 4
