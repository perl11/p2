#!perl
# p2-only: uses p2 extensions or p2 semantics that differ from perl; not cross-checked
# a collection while P->line (parser state) pointed into the dying birth region
# crashed in potion_mark_major (my %b = (1..50000) forces a full GC).
# Expected stdout is pinned with inline markers, compared by test/runtests.sh -p5.
my %b = (1..50000);
say 1; #=> 1
my @k = keys(%b); say @k->length; #=> 25000
