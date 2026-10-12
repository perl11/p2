#!/bin/bash
# Benchmark summary table: the same program in potion (.pn, the base), p5 (.pl,
# run by p2 and by perl) and p6 (.p6, run by p2 and by raku).
#
#   test/bench-summary.sh [name ...]       default: ackermann fib nqueens spectral-norm
#
# RUNS=3 takes the best of three runs, TIMEOUT=60 limits a single run (seconds),
# P2, POTION, PERL and RAKU name the interpreters. Times are wall clock seconds;
# "x pn" is the slowdown against the potion run, "-" a missing program or
# interpreter, ">T" a run that hit the timeout, "FAIL" a non-zero exit.
export LC_ALL=C # printf and bc need a decimal point
cd "$(dirname "$0")/.." || exit 1
P2=${P2:-bin/p2}; POTION=${POTION:-bin/potion}; PERL=${PERL:-perl}; RAKU=${RAKU:-raku}
RUNS=${RUNS:-1}; TIMEOUT=${TIMEOUT:-60}
names=("$@"); [ ${#names[@]} -eq 0 ] && names=(ackermann fib nqueens spectral-norm)

# best wall time of RUNS runs of the command, or ">T" / "FAIL" / "-"
timeit() {
  local file=$1; shift
  [ -f "$file" ] || { echo "-"; return; }
  command -v "$1" >/dev/null 2>&1 || [ -x "$1" ] || { echo "-"; return; }
  local best="" i s e rc
  for ((i = 0; i < RUNS; i++)); do
    s=$(date +%s.%N)
    timeout "$TIMEOUT" "$@" >/dev/null 2>&1; rc=$?
    e=$(date +%s.%N)
    [ $rc -eq 124 ] && { echo ">T"; return; }
    [ $rc -ne 0 ] && { echo "FAIL"; return; }
    s=$(echo "$e - $s" | bc -l)
    if [ -z "$best" ] || [ "$(echo "$s < $best" | bc -l)" = 1 ]; then best=$s; fi
  done
  printf "%.3f" "$best"
}
# "t (Nx)" of time t against base b, or just t
rel() {
  local t=$1 b=$2
  case "$t$b" in *-*|*">T"*|*FAIL*) printf "%s" "$t"; return ;; esac
  if [ "$(echo "$b > 0" | bc -l)" = 1 ]; then
    printf "%s (%.1fx)" "$t" "$(echo "$t / $b" | bc -l)"
  else printf "%s" "$t"; fi
}

printf "| %-13s | %-8s | %-18s | %-18s | %-18s | %-18s |\n" \
  program "pn" "pl p2 (x pn)" "pl perl (x pn)" "p6 p2 (x pn)" "p6 raku (x pn)"
printf "|%s|%s|%s|%s|%s|%s|\n" "$(printf -- '-%.0s' {1..15})" "$(printf -- '-%.0s' {1..10})" \
  "$(printf -- '-%.0s' {1..20})" "$(printf -- '-%.0s' {1..20})" "$(printf -- '-%.0s' {1..20})" "$(printf -- '-%.0s' {1..20})"
for n in "${names[@]}"; do
  pn=$(timeit "example/$n.pn" "$POTION" "example/$n.pn")
  plp2=$(timeit "example/$n.pl" "$P2" "example/$n.pl")
  plperl=$(timeit "example/$n.pl" "$PERL" "example/$n.pl")
  p6p2=$(timeit "example/$n.p6" "$P2" "example/$n.p6")
  p6raku=$(timeit "example/$n.p6" "$RAKU" "example/$n.p6")
  printf "| %-13s | %-8s | %-18s | %-18s | %-18s | %-18s |\n" "$n" "$pn" \
    "$(rel "$plp2" "$pn")" "$(rel "$plperl" "$pn")" "$(rel "$p6p2" "$pn")" "$(rel "$p6raku" "$pn")"
done
