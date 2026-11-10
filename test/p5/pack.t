#!perl
# pack/unpack builtins (lib/p5/p5pack.c). Cross-checked against perl 5.42.3.
# Hex is shown via unpack("H*", ...) (lowercase in both). Byte inputs use \xNN.
# @N is omitted (a no-op in perl 5.42.3) and high-byte \x80 is avoided (p2 stores
# it UTF-8, a byte/char limitation, not a pack bug). W>255 and U0 are omitted
# (perl keeps the full W value / U0 unpacks empty; p2 truncates / fills).
sub hx { unpack("H*", $_[0]) }
my $s;
# string types
$s = pack("A", "hello");    print hx($s), " ", length($s), "\n";  #=> 68 1
$s = pack("a", "hello");    print hx($s), " ", length($s), "\n";  #=> 68 1
$s = pack("A2", "hello");   print hx($s), " ", length($s), "\n";  #=> 6865 2
$s = pack("a2", "hello");   print hx($s), " ", length($s), "\n";  #=> 6865 2
$s = pack("A*", "hi");      print hx($s), " ", length($s), "\n";  #=> 6869 2
$s = pack("a*", "hi");      print hx($s), " ", length($s), "\n";  #=> 6869 2
$s = pack("A3", "hi");      print hx($s), " ", length($s), "\n";  #=> 686920 3
$s = pack("Z2", "hi");      print hx($s), " ", length($s), "\n";  #=> 6800 2
$s = pack("A3", "hello");   print hx($s), " ", length($s), "\n";  #=> 68656c 3
# char / integer types (C/W truncate, c/s/i/l sign)
print unpack("C", pack("C", 65)), " ", unpack("C", pack("C", 300)), "\n";  #=> 65 44
print unpack("c", pack("c", 65)), " ", unpack("c", pack("c", -1)), "\n";   #=> 65 -1
print unpack("W", pack("W", 65)), " ", unpack("W", pack("W", 255)), "\n";  #=> 65 255
print unpack("S", pack("S", 1000)), " ", unpack("s", pack("s", -2)), "\n"; #=> 1000 -2
print unpack("I", pack("I", 70000)), " ", unpack("i", pack("i", -1)), "\n"; #=> 70000 -1
print unpack("L", pack("L", 3000000000)), " ", unpack("l", pack("l", -2000000000)), "\n"; #=> 3000000000 -2000000000
print unpack("n", pack("n", 1000)), " ", unpack("v", pack("v", 1000)), "\n"; #=> 1000 1000
print unpack("N", pack("N", 100000)), " ", unpack("V", pack("V", 100000)), "\n"; #=> 100000 100000
# endianness: n/N big, v/V little, s/i/l native (little on x86)
print hx(pack("n", 1)), " ", hx(pack("v", 1)), " ", hx(pack("N", 1)), " ", hx(pack("V", 1)), "\n"; #=> 0001 0100 00000001 01000000
# hex (H: high nibble first, h: low first; both lowercase)
print unpack("H2", pack("H2", "ab")), " ", unpack("h2", pack("h2", "ab")), "\n"; #=> ab ab
print unpack("H*", pack("H*", "abcd12")), "\n";  #=> abcd12
print unpack("h*", pack("h*", "abcd12")), "\n";  #=> abcd12
# bits (B: MSB first, b: LSB first)
print unpack("B8", pack("B8", "11001001")), " ", unpack("b8", pack("b8", "10011100")), "\n"; #=> 11001001 10011100
print unpack("B*", pack("B*", "1100100111110000")), "\n";  #=> 1100100111110000
print unpack("b1", pack("b1", "1")), " ", unpack("B1", pack("B1", "1")), "\n"; #=> 1 1
# bignum BER
print unpack("w", pack("w", 300)), " ", unpack("w", pack("w", 123456)), " ", unpack("w", pack("w", 0)), " ", unpack("w", pack("w", 32767)), "\n"; #=> 300 123456 0 32767
print hx(pack("w", 300)), " ", hx(pack("w", 123456)), "\n";  #=> 822c 87c440
# floats
printf "f=%g d=%g F=%g\n", unpack("f", pack("f", 1.5)), unpack("d", pack("d", 1.5)), unpack("F", pack("F", 2.5));  #=> f=1.5 d=1.5 F=2.5
# unicode byte
print unpack("U", pack("U", 65)), "\n";  #=> 65
# position advance (x pads NUL)
print hx(pack("CCx2C", 1, 2, 3)), " ", length(pack("CCx2C",1,2,3)), "\n"; #=> 0102000003 5
print hx(pack("x2")), " ", length(pack("x2")), "\n";  #=> 0000 2
# unpack
print unpack("A", "he"), " ", unpack("A2", "he"), " ", unpack("a*", "hello"), "\n"; #=> h he hello
print unpack("C", "A"), " ", join(",", unpack("C2", "AB")), "\n";  #=> 65 65,66
print join(",", unpack("C C", "AB")), "\n";  #=> 65,66
print join(",", unpack("CCx2C", "\x01\x02\x00\x00\x03")), "\n";    #=> 1,2,3
print unpack("S", pack("S",1000)), " ", unpack("N", pack("N",100000)), "\n"; #=> 1000 100000
# scalar context gives the first element; list context the full list
print unpack("C C", "AB"), "\n";  #=> 6566
my @l = unpack("C*", "ABC"); print "list:", join(",", @l), "\n";  #=> list:65,66,67
