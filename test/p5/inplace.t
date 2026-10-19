#!perl
# $^I in-place edit through <>, print to the selected handle, @b = @a copies,
# \@a is one call argument, readpipe/system/$?
my $f = "inplace-test.tmp";
open(my $o, '>', $f) or die; print $o "foo\nfoo\n"; close $o;
$^I = '.bak';
@ARGV = ($f);
while (<>) { s/foo/bar/; } continue { print; }
$^I = undef;
@ARGV = ($f, "$f.bak");
my @l;
while (<>) { chomp; push @l, $_; }
say join(",", @l);                       #=> bar,bar,foo,foo
unlink($f); unlink("$f.bak");

my @a = (1, 2, 3);
my @b = @a;
shift @b;
say scalar(@a), "|", scalar(@b);         #=> 3|2
sub n { return scalar(@_) }
say n(\@a), "|", n(\@a, 'x');            #=> 1|2

my $r = readpipe("echo hi");
say length($r), "|", $r eq "hi\n" ? "same" : "diff";   #=> 3|same
system("exit 3;");
say $? >> 8;                             #=> 3
