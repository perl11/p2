#!perl
# p2 regression test: (EXPR) is plain grouping in scalar contexts: return, conditions, modifiers
sub f { return (1 == 2) }
say f() ? "true" : "false";                #=> false
if ((1 == 2)) { say "bad" } else { say "good" }   #=> good
say "ok" if ((1 == 2) || 0) == 0;          #=> ok
say "yes" if (2 > 1) == 1;                 #=> yes
say "unless" unless (1 == 2);              #=> unless
my $x = 0;
say "assigned" if (($x = 1));              #=> assigned
