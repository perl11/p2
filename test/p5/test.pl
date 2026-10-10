# test.pl - a small TAP library for the roast5 files, in the subset of Perl
# that both perl and p2 understand (perl's own t/test.pl is far larger than
# what p2 can parse yet). Only the commonly used functions are provided.
#
# Subs using @_ are compiled by p2 with at most 12 arguments. skip() cannot
# leave the enclosing SKIP block (no dynamic "last" in p2): the block runs on.

$TESTNUM = 0;
$PLANNED = 0;

sub plan {
    my ($a, $b) = @_;
    if ($a eq 'skip_all') {
        print "1..0 # SKIP $b\n";
        exit 0;
    }
    if ($a eq 'tests') { $a = $b; }
    $PLANNED = $a;
    print "1..$a\n";
}

sub skip_all {
    my $why = shift;
    print "1..0 # SKIP $why\n";
    exit 0;
}

sub _report {
    my ($pass, $name) = @_;
    $TESTNUM = $TESTNUM + 1;
    my $line = "ok $TESTNUM";
    if (!$pass) { $line = "not ok $TESTNUM"; }
    if (defined $name) { $line = "$line - $name"; }
    print "$line\n";
    return $pass;
}

sub ok {
    my ($pass, $name) = @_;
    _report($pass, $name);
}

sub pass {
    my $name = shift;
    _report(1, $name);
}

sub fail {
    my $name = shift;
    _report(0, $name);
}

sub is {
    my ($got, $expected, $name) = @_;
    my $pass = 0;
    if (!defined $got && !defined $expected) { $pass = 1; }
    elsif (defined $got && defined $expected && $got eq $expected) { $pass = 1; }
    _report($pass, $name);
    if (!$pass) {
        diag("         got: $got");
        diag("    expected: $expected");
    }
    $pass;
}

sub isnt {
    my ($got, $unexpected, $name) = @_;
    my $pass = 1;
    if (defined $got && defined $unexpected && $got eq $unexpected) { $pass = 0; }
    _report($pass, $name);
}

sub skip {
    my ($why, $n) = @_;
    if (!defined $n) { $n = 1; }
    my $i = 0;
    while ($i < $n) {
        $TESTNUM = $TESTNUM + 1;
        print "ok $TESTNUM # skip $why\n";
        $i = $i + 1;
    }
}

sub diag {
    my $msg = shift;
    print STDERR "# $msg\n";
}

sub note {
    my $msg = shift;
    print "# $msg\n";
}

sub done_testing {
    if ($PLANNED == 0) { print "1..$TESTNUM\n"; }
}

sub done { done_testing(); }

1;
