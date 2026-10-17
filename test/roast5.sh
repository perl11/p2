#!/bin/sh
# Run test/roast5 files from inside test/roast5, as perl's t/ expects:
# the tests do "require './test.pl'" relative to the cwd.
# test.pl lives in test/p5/ (p2 repo), not in the roast5 submodule; it is
# linked in for the duration of the run and removed afterwards, so the
# submodule stays clean in git status.
# usage: test/roast5.sh [-e interpreter] [prove-args] [files relative to test/roast5]
# default interpreter: perl; use "-e bin/p2" for p2.
root=$(cd "$(dirname "$0")/.." && pwd)
interp=perl
if [ "$1" = "-e" ]; then
  case $2 in /*) interp=$2;; */*) interp=$root/$2;; *) interp=$2;; esac
  shift 2
fi
dir=$root/test/roast5
# some tests (cmd/mod.t) read perl's t/TEST, any file of 50..1000 lines will do
if ! test -e "$dir/test.pl"; then
    ln -sf "$root/test/p5/test.pl" "$dir/test.pl"
    ln -sf "$root/test/p5/test.pl" "$dir/TEST"
    trap 'rm -f "$dir/test.pl" "$dir/TEST"' EXIT INT TERM
fi
cd "$dir" || exit 1
[ $# -eq 0 ] && set -- base
# accept paths given relative to the repo root
for a; do shift; set -- "$@" "${a#test/roast5/}"; done
prove -e "$interp" -I"$root/test/p5" "$@"
