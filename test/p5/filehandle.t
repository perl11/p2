#!perl
# p5 file handles: open (2/3-arg), print/say FH, <FH>, while (<FH>), eof, close.
# Expected stdout is pinned with inline markers, compared by test/runtests.sh -p5.
my $f = "/tmp/p2_filehandle_test.tmp";
open(my $o, '>', $f) || die "cannot open";
print $o "l1\n";
print {$o} "l2\n";
say $o "l3";
say close($o); #=> true
open(IN, $f) or die "cannot read";
my $n = 0;
while (<IN>) { $n++; }
say $n; #=> 3
say eof(IN); #=> true
say close(IN); #=> true
say close(IN); #=> false
open(IN, "<$f");
my $first = <IN>;
print $first; #=> l1
open(OUT, ">>", $f);
print OUT "l4\n";
close OUT;
$n = 0;
while (my $l = <IN>) { $n++; }
say $n; #=> 3
close IN;
open(NO, "<", "/nonexistent/x") or say "failed: $!"; #=> failed: No such file or directory
say "done"; #=> done
