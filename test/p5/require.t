#!perl
# p2-specific regression test: parse-time 'require "file"' (also inside BEGIN)
# using the roast5 TAP library, and the 'defined' operator.
# Compared by test/runtests.sh -p5; run from the repo root, so the library is
# found via the relative path.
BEGIN { require "test/p5/test.pl"; }
$x = 3;
if (defined $x && defined $x && $x eq 4) { say "wrong"; } else { say "defined ok"; } #=> defined ok
ok(1, "from required lib"); #=> ok 1 - from required lib
is(2, 3, "differ"); #=> not ok 2 - differ
say "done"; #=> done
