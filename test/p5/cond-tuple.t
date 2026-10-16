#!perl
# p2-specific regression test: a tuple (an array reference) as ?: / if condition is true,
# also the empty one, in the bytecode VM as in the JIT (the VM used to test `!empty` inverted).
# Expected stdout is pinned with inline markers (test/runtests.sh -p5).
sub full { return [1, 2] }
sub empty { return [] }
print full() ? "t" : "f", "\n"; #=> t
print empty() ? "t" : "f", "\n"; #=> t
my $c = full();
if ($c) { print "yes\n"; } else { print "no\n"; } #=> yes
my @a = (1);
print @a ? "t" : "f", "\n"; #=> t
my @e = ();
print @e ? "t" : "f", "\n"; #=> f
