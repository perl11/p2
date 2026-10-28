# test.pl - a small TAP library for the roast5 files, in the subset of Perl
# that both perl and p2 understand (perl's own t/test.pl is far larger than
# what p2 can parse yet). Only the commonly used functions are provided.
#
# Subs using @_ are compiled by p2 with at most 12 arguments. skip() cannot
# leave the enclosing SKIP block (no dynamic "last" in p2): the block runs on.

$TESTNUM = 0;
$PLANNED = 0;
$NOPLAN = 0;

sub plan {
    my ($a, $b) = @_;
    if ($a eq 'skip_all') {
        print "1..0 # SKIP $b\n";
        exit 0;
    }
    if ($a eq 'no_plan') { $NOPLAN = 1; return; }   # the plan 1..N is printed at END
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
    if (defined $::TODO && length($::TODO)) { $line = "$line # TODO $::TODO"; }  # a todo test may fail
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

sub cmp_ok {
    my ($got, $op, $expected, $name) = @_;
    my $pass = 0;
    if ($op eq '==') { $pass = ($got == $expected); }
    elsif ($op eq '!=') { $pass = ($got != $expected); }
    elsif ($op eq '<') { $pass = ($got < $expected); }
    elsif ($op eq '>') { $pass = ($got > $expected); }
    elsif ($op eq '<=') { $pass = ($got <= $expected); }
    elsif ($op eq '>=') { $pass = ($got >= $expected); }
    elsif ($op eq 'eq') { $pass = ($got eq $expected); }
    elsif ($op eq 'ne') { $pass = ($got ne $expected); }
    _report($pass, $name);
    if (!$pass) {
        diag("         got: $got");
        diag("    expected: $op $expected");
    }
    $pass;
}

sub like {
    my ($got, $re, $name) = @_;
    my $pass = 0;
    if (defined $got && $got =~ $re) { $pass = 1; }
    _report($pass, $name);
    if (!$pass) { diag("         got: $got"); }
    $pass;
}

sub unlike {
    my ($got, $re, $name) = @_;
    my $pass = 1;
    if (defined $got && $got =~ $re) { $pass = 0; }
    _report($pass, $name);
}

sub diag {
    my $msg = shift;
    print STDERR "# $msg\n";
}

sub note {
    my $msg = shift;
    print "# $msg\n";
}

# the number of the next test (perl's t/test.pl: $test starts at 1)
sub curr_test { return $TESTNUM + 1; }
sub next_test {
    $TESTNUM = $TESTNUM + 1;
    return $TESTNUM;
}

# eq_array(\@a, \@b): the same elements as strings
sub eq_array {
    my ($a, $b) = @_;
    if (scalar(@$a) != scalar(@$b)) { return 0; }
    my $i = 0;
    while ($i < scalar(@$a)) {
        if ($a->[$i] ne $b->[$i]) { return 0; }
        $i = $i + 1;
    }
    return 1;
}

# object_ok($obj, $class): a blessed object of the class
sub object_ok {
    my ($o, $c, $name) = @_;
    return ok(ref($o) && UNIVERSAL::isa($o, $c), $name || "the object is a $c");
}
sub isa_ok { return object_ok(@_); }

sub done_testing {
    if ($PLANNED == 0) { print "1..$TESTNUM\n"; }
}

sub done { done_testing(); }

# unique file names in the current directory, and their removal
$TMPN = 0;
sub tempfile {
    $TMPN = $TMPN + 1;
    my $f = "tmp" . $$ . "_" . $TMPN;
    $TMPFILES{$f} = 1;
    return $f;
}
END { foreach my $f (keys %TMPFILES) { unlink($f); unlink("$f.bak"); } }

sub unlink_all {
    my $n = 0;
    foreach my $f (@_) { if (unlink($f)) { $n = $n + 1; } }
    return $n;
}

sub which_perl { return $^X; }

# runperl(prog => CODE, args => [..], stderr => 1, stdin => TEXT): the output of a new
# interpreter running CODE with -e; an arg like '>' or a file name goes to the shell as is
sub runperl {
    my %a = @_;
    my $prog = $a{prog};
    $prog =~ s/'/'\\''/g;
    my $cmd = $^X . " -e '" . $prog . "'";
    if (defined $a{args}) {
        foreach my $arg (@{$a{args}}) { $cmd = $cmd . " " . $arg; }
    }
    if ($a{stderr}) { $cmd = $cmd . " 2>&1"; }
    if (defined $a{stdin}) { $cmd = "printf '%s' '" . $a{stdin} . "' | " . $cmd; }
    return readpipe($cmd);
}

END { if ($NOPLAN) { done_testing(); } }

1;
