#!/bin/sh
# run_p6_test.sh - run a p6 test file with #=> expected output
testfile="$1"
P2=./bin/p2
while IFS= read -r line; do
  case "$line" in
    *"#=>"*)
      expr=$(echo "$line" | sed 's/.*use p6 { \(.*\) }.*#=>.*/\1/')
      expected=$(echo "$line" | sed 's/.*#=> //')
      got=$($P2 -6 -e "say($expr)" 2>/dev/null)
      if [ "$got" = "$expected" ]; then
        echo "ok - $testfile: $expr"
      else
        echo "not ok - $testfile: $expr expected <$expected> got <$got>"
      fi
      ;;
  esac
done < "$testfile"
