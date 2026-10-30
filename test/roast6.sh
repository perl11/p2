#!/bin/sh
# Run test/roast6 files from inside test/roast6 with `p2 -6` (the tests `use Test`, a native module of libp6).
# usage: test/roast6.sh [prove-args] [files relative to test/roast6]
root=$(cd "$(dirname "$0")/.." && pwd)

# roast6 files that pass (python3 test/newok6.py --write adds the new ones);
# run when no files are given.
OK_ROAST6="S03-operators/assign-is-not-binding.t S17-lowlevel/thread-start-join-stress.t S32-list/are.t integration/advent2012-day24.t

S01-perl-5-integration/class.t
S01-perl-5-integration/context.t
S01-perl-5-integration/exception_handling.t
S01-perl-5-integration/import.t
S01-perl-5-integration/isms.t
S01-perl-5-integration/strings.t
S01-perl-5-integration/subs.t
S02-lexical-conventions/bom.t
S02-lexical-conventions/pod-in-multi-line-exprs.t
S02-literals/pair-boolean.t
S02-types/parsing-bool.t
S03-smartmatch/scalar-hash.t
S04-statements/no-implicit-block.t
S04-statements/until.t
S06-signature/closure-over-parameters.t
S06-signature/scalar-type.t
S14-roles/bool.t
S15-nfg/cgj.t
S15-nfg/grapheme-break.t
S17-channel/subscription-drain-in-react.t
S17-promise/lock-async-stress2.t
S24-testing/0-compile.t
S28-named-variables/cwd.t
S29-context/exit-in-if.t
S32-io/other.t
S32-trig/pi.t
S32-trig/simple.t
integration/advent2009-day03.t
integration/eval-and-threads.t
integration/lexical-array-in-inner-block.t
integration/method-calls-and-instantiation.t
integration/passing-pair-class-to-sub.t
integration/substr-after-match-in-gather-in-for.t
integration/topic_in_double_loop.t
"

cd "$root/test/roast6" || exit 1
[ $# -eq 0 ] && set -- $OK_ROAST6
for a; do shift; set -- "$@" "${a#test/roast6/}"; done
exec prove -e "$root/bin/p2 -6" "$@"
