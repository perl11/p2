#!/bin/sh
# run_p6_test.sh - run a p6 test file (test/p6/*.p6) and compare its stdout
# with the expected output given in '#=> text' comments.
#
# The test files are plain Raku scripts: every line that prints something ends
# in '#=> <line of expected output>', and the expected output of the whole
# file is those texts in order, one per output line.
#
# The script is run with p2 (./bin/p2 -6). If a raku interpreter is installed
# (RAKU env var, default `raku`; RAKU= disables) the same file is also run with
# it and compared against the same expectation, which cross-checks p2 against
# the reference implementation and keeps the tests valid Raku.
# Exit status is 1 if any comparison failed.
testfile="$1"
P2=${P2:-./bin/p2}
RAKU=${RAKU-raku}

if [ ! -f "$testfile" ]; then
  echo "not ok - $testfile: no such file"
  exit 1
fi

expected=$(sed -n 's/.*#=> \{0,1\}//p' "$testfile")
rc=0

check() { # name, command...
  name="$1"; shift
  got=$("$@" 2>/dev/null)
  if [ "$got" = "$expected" ]; then
    echo "ok - $testfile ($name)"
  else
    echo "not ok - $testfile ($name)"
    echo "  expected:"
    echo "$expected" | sed 's/^/    /'
    echo "  got:"
    echo "$got" | sed 's/^/    /'
    rc=1
  fi
}

check p2 "$P2" -6 "$testfile"
if [ -n "$RAKU" ] && command -v "$RAKU" >/dev/null 2>&1; then
  check raku "$RAKU" "$testfile"
else
  echo "# $testfile: no raku found, skipping cross-compare"
fi
exit $rc
