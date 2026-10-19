#!/bin/sh
# Run test/roast6 files from inside test/roast6 with `p2 -6` (the tests `use Test`, a native module of libp6).
# usage: test/roast6.sh [prove-args] [files relative to test/roast6]
root=$(cd "$(dirname "$0")/.." && pwd)

# roast6 files that pass (python3 test/newok6.py --write adds the new ones);
# run when no files are given.
OK_ROAST6="S04-phasers/eval-in-begin.t S04-statement-modifiers/values_in_bool_context.t integration/rule-in-class-Str.t
S04-statements/no-implicit-block.t S32-hash/push.t S32-trig/pi.t S32-trig/simple.t
S32-trig/e.t S02-literals/subscript.t S03-operators/fiddly.t S04-statements/sink.t
S04-statements/unless.t S12-construction/autopairs.t S32-str/lines.t
S01-perl-5-integration/basic.t S01-perl-5-integration/class.t
S01-perl-5-integration/exception_handling.t S01-perl-5-integration/import.t
S02-literals/pair-boolean.t S16-filehandles/connect.t
integration/method-calls-and-instantiation.t integration/passing-pair-class-to-sub.t
integration/substr-after-match-in-gather-in-for.t S02-types/parsing-bool.t
S02-lexical-conventions/bom.t S02-lexical-conventions/end-pod.t
S02-lexical-conventions/pod-in-multi-line-exprs.t S04-statements/until.t
S06-signature/closure-over-parameters.t S06-signature/scalar-type.t
S11-modules/use-perl-6.t S12-methods/qualified.t S14-roles/bool.t S16-filehandles/open.t
S24-testing/0-compile.t S28-named-variables/cwd.t S29-context/exit-in-if.t
integration/advent2009-day03.t integration/lexical-array-in-inner-block.t
integration/topic_in_double_loop.t S02-lexical-conventions/begin_end_pod.t"

cd "$root/test/roast6" || exit 1
[ $# -eq 0 ] && set -- $OK_ROAST6
for a; do shift; set -- "$@" "${a#test/roast6/}"; done
exec prove -e "$root/bin/p2 -6" "$@"
