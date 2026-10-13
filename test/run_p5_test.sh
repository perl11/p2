#!/bin/sh
# run_p5_test.sh - run a p5 regression test (test/p5/*.t) and compare its full
# stdout against the matching *.expected file.
# Unlike test/run_p6_test.sh (one '#=> expected' per output line), these
# are self-contained scripts whose whole output is in a sibling *.expected.
#
# The script is run with p2 (forced into p5 mode by the .t extension). If a perl
# interpreter is installed (PERL env var, default `perl`; PERL= disables) the same
# file is also run with it (with `say` enabled) and compared against the same
# expectation, which cross-checks p2 against the reference implementation and
# keeps the tests valid Perl 5.
# Exit status is 1 if any comparison failed.
testfile="$1"
P2=${P2:-./bin/p2}
PERL=${PERL-perl}
expected="${testfile%.t}.expected"

if [ ! -f "$expected" ]; then
  echo "not ok - $testfile: missing $expected"
  exit 1
fi

want=$(tr -d '\015' <"$expected")
rc=0

check() { # name, command...
  name="$1"; shift
  got=$("$@" 2>/dev/null | tr -d '\015')
  if [ "$got" = "$want" ]; then
    echo "ok - $testfile ($name)"
  else
    echo "not ok - $testfile ($name)"
    echo "  expected:"
    echo "$want" | sed 's/^/    /'
    echo "  got:"
    echo "$got" | sed 's/^/    /'
    rc=1
  fi
}

# the p2 libraries (libp2, libsyntax-p6, ..) are found without an rpath too, but not for raku/perl
check p2 env LD_LIBRARY_PATH="lib:$LD_LIBRARY_PATH" "$P2" "$testfile"
if [ -n "$PERL" ] && command -v "$PERL" >/dev/null 2>&1; then
  check perl "$PERL" -Mfeature=say "$testfile"
else
  echo "# $testfile: no perl found, skipping cross-compare"
fi
exit $rc
