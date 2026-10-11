#!perl
# my (LIST) = @$ref / @{$ref}: elements read as $ref->[i]; a long single-quoted
# string in a VALUE node must not be mistaken for a lick AST node.
# Expected stdout is pinned with inline markers, compared by test/runtests.sh -p5.
my $r = [5, 6];
my ($c, $d) = @$r;
say $d; #=> 6
my ($e, $f, $g) = @{$r};
say $f; #=> 6
say $g; #=> undef
my ($x) = ($r);
say $x->length; #=> 2
sub is { return $_[1] }
say is(join(' ', @$r), 'x y z', 'void unshift singleton list (ref)'); #=> x y z
# an empty loop body or an empty list must not crash p5_forlist
for my $k (1, 2) {}
for my $k () { say "never" }
say "loops ok"; #=> loops ok
# control keywords directly followed by ( or { (no space)
my $kw = 2;
if($kw==1){ print "a"; }elsif($kw==2){ print "b"; }else{ print "c"; }
say ""; #=> b
# a nested if/elsif/else must not clobber the condition of the outer else,
# and an assignment can be the condition
my $oc = 1;
if (my $oe = $oc) {
    if ($oe =~ /A/) { print "1"; }
    elsif ($oe =~ /R/) { print "2"; }
    else { print "3"; }
}
else { print "4"; }
say ""; #=> 3
our $LT = 1;
{ local $LT = 5 if 1; print $LT; }
say $LT; #=> 51
# '' is false (also in the JIT, which compared the wrong word), and NOT agrees
my $es = '';
if ($es) { print "T"; } else { print "F"; }
unless ($es) { print "U"; }
say ""; #=> FU
# print(LIST) with parens, print/say return true
print("a", "b");
say(1+2); #=> ab3
say((print "") ? "t" : "f"); #=> t
# each on arrays and hashes, list assignment as a condition (its value is the element count)
my @ea = ('x', 'y');
while (my ($ei, $ev) = each @ea) { print "$ei=$ev "; }
say ""; #=> 0=x 1=y
my %eh = (k => 1);
while (my ($ek, $evv) = each %eh) { print "$ek=$evv "; }
say ""; #=> k=1
my @emp = ();
if (my ($ez) = @emp) { say "bad"; } else { say "empty"; } #=> empty
