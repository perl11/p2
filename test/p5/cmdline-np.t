#!perl
# the -n / -p / -i[ext] / -e switches of the interpreter ($^X), checked through the shell
my $f = "cmdline-np.tmp";
open(my $o, '>', $f) or die; print $o "foo a\nfoo b\n"; close $o;
my $r = readpipe("$^X -ne 'print if /a/' $f");
print $r;                                 #=> foo a
$r = readpipe("$^X -pe 's/foo/bar/' $f");
print join("|", split(/\n/, $r)), "\n";   #=> bar a|bar b
system("$^X -pi.bak -e 's/foo/baz/' $f");
$r = readpipe("cat $f $f.bak");
print join("|", split(/\n/, $r)), "\n";   #=> baz a|baz b|foo a|foo b
$r = readpipe("$^X -e 'print qq(\@ARGV\\n)' x y");
print $r;                                 #=> x y
open($o, '>', $f) or die; print $o "a:b c\nd:e f\n"; close $o;
$r = `$^X -F: -lane 'print \$F[1]' $f`;
print join("|", split(/\n/, $r)), "\n";        #=> b c|e f
$r = qx{$^X -lane 'print \$F[1]' $f};
print join("|", split(/\n/, $r)), "\n";        #=> c|f
$r = `$^X -0777 -ne 'print length(\$_)' $f`;
print $r, "\n";                                #=> 12
$r = `$^X -lpe '\$_ .= "!"' $f`;
print join("|", split(/\n/, $r)), "\n";        #=> a:b c!|d:e f!
my @l = `cat $f`;
print scalar(@l), "\n";                        #=> 2
unlink($f); unlink("$f.bak");
