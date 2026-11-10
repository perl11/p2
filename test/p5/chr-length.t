#!perl
# chr() of a high codepoint is a UTF-8 string: length counts characters,
# ord returns the codepoint. chr(0x80-0xff) used to be a lone continuation
# byte (invalid UTF-8): length was 0 and ord was 0.
print length(chr(0x80)), " ", length(chr(0x9e)), " ", length(chr(0xff)), "\n";   #=> 1 1 1
print length(chr(0x9e).chr(0x9d)), "\n";                                        #=> 2
print length(chr(0x100)), " ", length(chr(0x20ac)), "\n";                       #=> 1 1
print ord(chr(0x80)), " ", ord(chr(0x9e)), " ", ord(chr(0xff)), " ",
      ord(chr(0x100)), " ", ord(chr(0x20ac)), "\n";                             #=> 128 158 255 256 8364
print length("abc"), " ", length("\x9e\x9d"), "\n";                             #=> 3 2
