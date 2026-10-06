#!/bin/sh
# run_p5_test.sh - run a p2-specific p5 regression test (test/p5/*.t)
# and compare its full stdout against the matching *.expected file.
# Unlike test/run_p6_test.sh (one #=> expression per line, via -6 -e),
# these are small self-contained scripts exercising p5 control-flow
# and data-structure features (for/foreach, real hashes, sub/return,
# ||/&& precedence) that need a real .t file, not a single -e
# expression, forced into p5 mode by the .t extension.
testfile="$1"
P2=./bin/p2
expected="${testfile%.t}.expected"

if [ ! -f "$expected" ]; then
  echo "not ok - $testfile: missing $expected"
  exit 1
fi

got=$($P2 "$testfile" 2>/dev/null)
want=$(cat "$expected")

if [ "$got" = "$want" ]; then
  echo "ok - $testfile"
else
  echo "not ok - $testfile"
  echo "  expected:"
  echo "$want" | sed 's/^/    /'
  echo "  got:"
  echo "$got" | sed 's/^/    /'
fi
