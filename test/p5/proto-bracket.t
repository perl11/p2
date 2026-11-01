#!perl
# a prototype may contain \[$@%&*]; &name(..) calls bypass it
sub myref (\[$@%&*]) { return "ok:$_[0]" }
sub myref2 (\[$@]$;$) { return "two:$_[1]" }
print &myref("x"), "\n";                     #=> ok:x
print &myref2("a", "b"), "\n";               #=> two:b
my $f = sub (\[$@%]) { "anon:$_[0]" };
print &$f("z"), "\n";                        #=> anon:z
