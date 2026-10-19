#!/bin/sh
# Run test/roast5 files from inside test/roast5, as perl's t/ expects:
# the tests do "require './test.pl'" relative to the cwd.
# test.pl lives in test/p5/ (p2 repo), not in the roast5 submodule; it is
# linked in for the duration of the run and removed afterwards, so the
# submodule stays clean in git status.
# usage: test/roast5.sh [-e interpreter] [prove-args] [files relative to test/roast5]
# default files: OK_ROAST below. default interpreter: perl; use "-e bin/p2" for p2.
root=$(cd "$(dirname "$0")/.." && pwd)
interp=perl
if [ "$1" = "-e" ]; then
  case $2 in /*) interp=$2;; */*) interp=$root/$2;; *) interp=$2;; esac
  shift 2
fi
dir=$root/test/roast5

# roast5 files that pass (python3 test/newok5.py --write adds the new ones);
# run when no files are given. Not op/hash-rt85026.t: it needs Devel::Peek.
OK_ROAST="comp/term.t op/unlink.t
comp/multiline.t io/perlio_leaks.t lib/commonsense.t mro/inconsistent_c3.t mro/inconsistent_c3_utf8.t mro/overload_c3.t mro/overload_c3_utf8.t mro/overload_dfs.t op/chars.t op/int.t op/not.t op/sleep.t op/time_loop.t opbasic/qq.t porting/exec-bit.t run/noswitch.t run/script.t run/switch0.t run/switchF.t run/switcha.t run/switchn.t run/switchp.t
io/inplace.t io/iprefix.t io/errno.t

base/cond.t
base/if.t
base/pat.t
base/while.t
cmd/elsif.t
cmd/mod.t
cmd/switch.t
comp/cmdopt.t
mro/basic_01_c3.t
mro/basic_01_c3_utf8.t
mro/basic_01_dfs.t
mro/basic_01_dfs_utf8.t
mro/basic_02_c3.t
mro/basic_02_c3_utf8.t
mro/basic_02_dfs.t
mro/basic_02_dfs_utf8.t
mro/basic_03_c3.t
mro/basic_03_c3_utf8.t
mro/basic_03_dfs.t
mro/basic_03_dfs_utf8.t
mro/basic_04_c3.t
mro/basic_04_c3_utf8.t
mro/basic_04_dfs.t
mro/basic_04_dfs_utf8.t
mro/basic_05_c3.t
mro/basic_05_c3_utf8.t
mro/basic_05_dfs.t
mro/basic_05_dfs_utf8.t
mro/c3_with_overload.t
mro/c3_with_overload_utf8.t
mro/complex_c3.t
mro/complex_c3_utf8.t
mro/complex_dfs.t
mro/complex_dfs_utf8.t
mro/dbic_c3.t
mro/dbic_c3_utf8.t
mro/dbic_dfs.t
mro/dbic_dfs_utf8.t
mro/next_NEXT.t
mro/next_NEXT_utf8.t
mro/next_inanon.t
mro/next_inanon_utf8.t
mro/next_ineval.t
mro/next_method.t
mro/next_method_utf8.t
mro/next_skip.t
mro/vulcan_c3.t
mro/vulcan_c3_utf8.t
mro/vulcan_dfs.t
mro/vulcan_dfs_utf8.t
op/append.t
op/cond.t
op/context.t
op/die_except.t
op/exp.t
op/lock.t
op/overload_integer.t
porting/bincompat.t
re/no_utf8_pm.t
uni/eval.t
"

# some tests (cmd/mod.t) read perl's t/TEST, any file of 50..1000 lines will do
if ! test -e "$dir/test.pl"; then
    ln -sf "$root/test/p5/test.pl" "$dir/test.pl"
    ln -sf "$root/test/p5/test.pl" "$dir/TEST"
    ln -sfn . "$dir/t"   # require "t/test.pl"
    trap 'rm -f "$dir/test.pl" "$dir/TEST" "$dir/t"' EXIT INT TERM
fi
cd "$dir" || exit 1
if [ $# -eq 0 ]; then
  set -- $OK_ROAST
  case $(uname -s) in MINGW*|MSYS*|CYGWIN*) # these need a unix shell: -e quoting, perl -e pipes
    for a; do shift; case $a in io/inplace.t|io/iprefix.t|io/errno.t|comp/multiline.t|run/*) ;; *) set -- "$@" "$a";; esac; done;;
  esac
fi
# accept paths given relative to the repo root
for a; do shift; set -- "$@" "${a#test/roast5/}"; done
prove -e "$interp" -I"$root/test/p5" "$@"
