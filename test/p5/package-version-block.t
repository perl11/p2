#!perl
# package NAME VERSION BLOCK sets $NAME::VERSION (the block form with a version was parsed as a call of `package`)
package Foo 1.5 { sub v { $Foo::VERSION } }
print Foo::v(), "|", $Foo::VERSION, "\n"; #=> 1.5|1.5
package Bar 2.5;
print "$Bar::VERSION\n"; #=> 2.5
package Baz v1.2.3 { }
print "ok\n"; #=> ok
