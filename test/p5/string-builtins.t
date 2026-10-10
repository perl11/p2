#!perl
# p2-specific regression test: p5 string builtins lc uc substr index rindex
# join sprintf reverse (byte oriented). Omitted optional arguments must not
# read garbage.
# Expected stdout is pinned with inline markers, compared by
# test/runtests.sh -p5.
say lc("AB") . uc("cd"); #=> abCD
say lc "XY"; #=> xy
say substr("hello", 1, 3); #=> ell
say substr("hello", -3); #=> llo
say substr("hello", 2); #=> llo
say index("hello", "l"); #=> 2
say rindex("hello", "l"); #=> 3
say index("hello", "z"); #=> -1
say join(",", 1, 2, 3); #=> 1,2,3
my @a = (4, 5, 6);
say join("-", @a); #=> 4-5-6
say sprintf("%03d|%s|%5.2f|%x", 7, "hi", 3.14159, 255); #=> 007|hi| 3.14|ff
say sprintf("plain"); #=> plain
say reverse("abc"); #=> cba
