#!perl
# p2-specific regression test: 'package Foo::Bar;', 'use Module LIST;' (import
# lists are skipped) and punctuation special variables such as $|, compared
# by test/runtests.sh -p5.
use vars qw($x);
use Foo::Bar;
use strict;
$| = 1;
package Foo::Bar;
say "after package"; #=> after package
package Other { say "in block"; } #=> in block
say "done"; #=> done
sub fwd;
SKIP: { say "in label"; } #=> in label
