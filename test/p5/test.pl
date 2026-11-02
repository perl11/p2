# test.pl - a small TAP library for the roast5 files, in the subset of Perl
# that both perl and p2 understand (perl's own t/test.pl is far larger than
# what p2 can parse yet). Only the commonly used functions are provided.
#
# Subs using @_ are compiled by p2 with at most 12 arguments. A SKIP: { ... }
# block is really left on skip(): p2's parser sees the skip(..) call in the block
# (p5_loopctl_seen) and turns the labeled bare block into a one-shot loop
# (p5_bare_block), labels it (p5_label_stmt) and appends `last SKIP` after every
# skip(..) statement (p5_skip_walk) — see syn/syntax-p5.y.

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

# The real t/test.pl decides from %Config. p2 is a minimal build: every config option
# the roast5 suite asks about (useithreads, d_getppid, d_pipe, d_fork, d_waitpid,
# d_setpgrp, d_fcntl, usedtrace, mad) is absent, so skip_all_without_config always
# skips, and p2 is never miniperl, so skip_all_if_miniperl skips nothing. It does NOT
# read $Config here: a sub that reads $Config (or a key list) in this inlined test.pl,
# with the program also running a regex, hits a pre-existing p2 GC hazard (a stale
# reference that later fataled in potion_mark_major; repro re/reg_posixcc.t).
sub skip_all_without_config {
    my (@key) = @_;
    skip_all("no " . (defined $key[0] ? $key[0] : "config"));
}
sub skip_all_if_miniperl {
    my ($why) = @_;
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
# interpreter running CODE with -e; an arg like '>' or a file name goes to the shell as is.
# Like real perl's _create_runperl: switches/progfile come between $^X and -e, each
# switch element is one shell word (it may carry its own quoting).
sub runperl {
    my %a = @_;
    my $cmd = $^X;
    if (defined $a{switches}) {
        die "runperl(): 'switches' must be an ARRAYREF"
            unless ref $a{switches} eq "ARRAY";
        # each element goes to the shell as is (like real perl's _quote_args):
        # an element may carry its own quoting, e.g. "-nE", q("} END {..}")
        foreach my $sw (@{$a{switches}}) { $cmd .= " " . $sw; }
    }
    if (defined $a{prog}) {
        my $prog = $a{prog};
        $prog =~ s/'/'\\''/g;
        $cmd .= " -e '" . $prog . "'";
    } elsif (defined $a{progfile}) {
        $cmd .= " \"" . $a{progfile} . "\"";
    }
    if (defined $a{stdin}) { $cmd = "printf '%s' '" . $a{stdin} . "' | " . $cmd; }
    if (defined $a{args}) {
        foreach my $arg (@{$a{args}}) { $cmd = $cmd . " " . $arg; }
    }
    if ($a{stderr}) { $cmd = $cmd . " 2>&1"; }
    return readpipe($cmd);
}

# fresh_perl($prog, $opt): run $prog in a fresh interpreter ($^X -e), return its output.
# $opt: { stderr => 1, stdin => TEXT, switches => [...] } (all optional)
sub fresh_perl {
    my ($prog, $opt) = @_;
    my $stderr = (ref($opt) eq 'HASH' && $opt->{stderr}) ? 1 : 0;
    my $stdin  = (ref($opt) eq 'HASH') ? $opt->{stdin} : undef;
    my $sw = "";
    if (ref($opt) eq 'HASH' && defined $opt->{switches}) { $sw = join ' ', @{$opt->{switches}}; }
    $prog =~ s/'/'\\''/g;
    my $cmd = $^X . " -e '" . $prog . "'";
    if (length($sw)) { $cmd = $sw . " " . $cmd; }
    if ($stderr) { $cmd = $cmd . " 2>&1"; }
    if (defined $stdin) { $stdin =~ s/'/'\\''/g; $cmd = "printf '%s' '" . $stdin . "' | " . $cmd; }
    return readpipe($cmd);
}
# fresh_perl_is($prog, $expected, $opt_or_label, $name): run $prog fresh, output must equal $expected
# fresh_perl_like($prog, $re, $opt_or_label, $name): run $prog fresh, output must match the regex $re
# the 3rd arg is the options hashref, or the test label when the 4th arg is the name
sub fresh_perl_is {
    my ($prog, $expected, $a3, $a4) = @_;
    my $opt; my $name;
    if (ref($a3) eq 'HASH') { $opt = $a3; $name = $a4; }
    elsif (defined $a3) { $name = $a3; }
    else { $name = $a4; }
    is(fresh_perl($prog, $opt), $expected, $name);
}
sub fresh_perl_like {
    my ($prog, $re, $a3, $a4) = @_;
    my $opt; my $name;
    if (ref($a3) eq 'HASH') { $opt = $a3; $name = $a4; }
    elsif (defined $a3) { $name = $a3; }
    else { $name = $a4; }
    like(fresh_perl($prog, $opt), $re, $name);
}

END { if ($NOPLAN) { done_testing(); } }

1;
