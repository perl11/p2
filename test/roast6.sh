#!/bin/sh
# Run test/roast6 files from inside test/roast6 with `p2 -6` (the tests `use Test`, a native module of libp6).
# usage: test/roast6.sh [prove-args] [files relative to test/roast6]
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root/test/roast6" || exit 1
for a; do shift; set -- "$@" "${a#test/roast6/}"; done
exec prove -e "$root/bin/p2 -6" "$@"
