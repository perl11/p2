#!perl
# \&{"name"}: a code reference to the sub a (computed) name refers to
sub t2 { 42 }
my $s = \&{"t2"};
print "r1\n" if ref($s) eq 'CODE';                     #=> r1
print "r2\n" if $s == \&t2;                            #=> r2
print $s->(), "\n";                                     #=> 42

# scoped to the current package
{
	package P;
	sub t9 { 99 }
	my $r = \&{"t9"};
	print "r3\n" if ref($r) eq 'CODE';                  #=> r3
	print "r4\n" if $r == \&t9;                         #=> r4
	print $r->(), "\n";                                 #=> 99
}
package main;

# a computed name from an expression
my $n = "t2";
print "r5\n" if \&{$n} == \&t2;                        #=> r5
