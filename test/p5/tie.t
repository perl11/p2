#!perl
# tie: scalars (TIESCALAR/FETCH/STORE), arrays (TIEARRAY ...), hashes (TIEHASH ...), tied, untie
package Counter;
sub TIESCALAR { my ($c, $v) = @_; my $x = $v; return bless \$x, $c }
sub FETCH { my $s = shift; print "FETCH\n"; return $$s++ }
sub STORE { my ($s, $v) = @_; print "STORE $v\n"; $$s = $v }
package main;
tie my $n, 'Counter', 5;
print "a=$n\n";
print "b=", $n + 0, "\n";
$n = 10;
print "c=$n\n";
print ref(tied $n), "\n";
untie $n;
package TA2;
my $q = 0;
sub quiet { $q = $_[0] if @_; $q }
sub TIEARRAY { my ($c) = @_; print "TIEARRAY\n"; bless { a => [] }, $c }
sub FETCH { my ($s, $i) = @_; print "FETCH $i\n" unless quiet(); $s->{a}[$i] }
sub STORE { my ($s, $i, $v) = @_; print "STORE $i $v\n"; $s->{a}[$i] = $v }
sub FETCHSIZE { my $s = shift; scalar @{$s->{a}} }
sub STORESIZE { }
sub PUSH { my $s = shift; print "PUSH @_\n"; push @{$s->{a}}, @_ }
sub POP { my $s = shift; print "POP\n"; pop @{$s->{a}} }
sub SHIFT { my $s = shift; print "SHIFT\n"; shift @{$s->{a}} }
sub CLEAR { my $s = shift; print "CLEAR\n"; $s->{a} = [] }
sub EXTEND { }
package TH2;
sub TIEHASH { bless { h => {}, }, shift }
sub FETCH { my ($s, $k) = @_; print "HFETCH $k\n"; $s->{h}{$k} }
sub STORE { my ($s, $k, $v) = @_; print "HSTORE $k $v\n"; $s->{h}{$k} = $v }
sub EXISTS { my ($s, $k) = @_; print "EXISTS $k\n"; exists $s->{h}{$k} }
sub DELETE { my ($s, $k) = @_; print "DELETE $k\n"; delete $s->{h}{$k} }
sub CLEAR { my $s = shift; print "HCLEAR\n"; $s->{h} = {} }
sub FIRSTKEY { my $s = shift; my @k = sort keys %{$s->{h}}; $s->{k} = \@k; shift @k }
sub NEXTKEY { my $s = shift; shift @{$s->{k}} }

tie my @a, "TA2";
$a[0] = 'x'; $a[1] = 'y';
print "e0=$a[0]\n";
push @a, 'z';
print "size=", scalar(@a), " last=$#a\n";
my $p = pop @a; print "popped $p\n";
print "join=", join(",", @a), "\n";
@a = (7, 8);
print "n=", scalar(@a), "\n";
TA2::quiet(1); for my $e (@a) { print "it $e\n" } TA2::quiet(0);
print ref(tied @a), "\n";
untie @a;
tie my %h, "TH2";
$h{a} = 1; $h{b} = 2;
print "h=$h{a}\n";
print "ex\n" if exists $h{b};
delete $h{a};
print join(",", keys %h), "\n";
%h = (x => 9);
print join(",", map { "$_=$h{$_}" } keys %h), "\n";
