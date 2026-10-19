#!perl
# the switches on the #! line of a script (-n -l -a -0), BEGIN blocks and __DATA__ of a -n script, a program on stdin
my $f = "cmdline-shebang.tmp";
open(my $o, '>', $f) or die; print $o "#!./perl -nal\nBEGIN { print 'start' }\nprint \$F[1];\n"; close $o;
my $in = "cmdline-shebang.in";
open($o, '>', $in) or die; print $o "a b\nc d\n"; close $o;
my $r = readpipe("$^X $f $in");
print join("|", split(/\n/, $r)), "\n";   #=> start|b|d
open($o, '>', $f) or die; print $o "#!./perl -0\nprint ord(\$/), \"\\n\";\n"; close $o;
print readpipe("$^X $f");                 #=> 0
open($o, '>', $f) or die; print $o "print 'stdin ', \$0, \"\\n\";\n"; close $o;
print readpipe("$^X < $f");               #=> stdin -
unlink $f, $in;
