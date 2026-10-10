#!perl
# p2-specific regression test: unless (COND) {...} [else {...}] statements.
# Expected stdout is pinned with inline markers, compared by
# test/runtests.sh -p5.
my $f = 0;
my $t = 1;
unless ($f) { say "run"; } #=> run
unless ($t) { say "bad"; }
unless ($f) { say "u"; } else { say "bad"; } #=> u
unless ($t) { say "bad"; } else { say "e"; } #=> e
say "end"; #=> end
