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
sub id { return $_[0] }
say id(((1 == 2)) && 1) ? "bad" : "good";   #=> good
say "or" if (0) or 1;                       #=> or
say "and" if (1 == 1) && (2 > 1);           #=> and
say id((0) || 5);                           #=> 5
