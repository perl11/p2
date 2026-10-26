#!/bin/bash
# Benchmark summary table: the same program in potion (.pn, the base for the pn
# columns), p5 (.pl, run by p2 and by perl) and p6 (.p6, run by p2 and by raku).
#
#   test/bench-summary.sh [name ...]       default: ackermann fib fib.p2 nqueens spectral-norm
#
# A name with a .p2 suffix (fib.p2) is a row for the p2 variant of the program,
# written in the subset p2 and perl both run: it fills the "pl p2" and "pl perl"
# columns with example/fib.p2 run by p2 and by perl.
#
# RUNS=3 takes the best of three runs, TIMEOUT=60 limits a single run (seconds),
# P2, POTION, PERL and RAKU name the interpreters. Times are wall clock seconds;
# "x pn" is the slowdown against the potion run, "x perl" / "x raku" against the perl / raku run, "-" a missing program or
# interpreter, ">T" a run that hit the timeout, "FAIL" a non-zero exit.
export LC_ALL=C # printf and bc need a decimal point
cd "$(dirname "$0")/.." || exit 1
P2=${P2:-bin/p2}; POTION=${POTION:-bin/potion}; PERL=${PERL:-perl}; RAKU=${RAKU:-raku}
RUNS=${RUNS:-1}; TIMEOUT=${TIMEOUT:-60}
names=("$@"); [ ${#names[@]} -eq 0 ] && names=(ackermann fib fib.p2 nqueens spectral-norm)

# --compile=exe of a program into a temp dir; prints the executable path or nothing
exedir=$(mktemp -d "${TMPDIR:-/tmp}/bench-exe.XXXXXX"); trap 'rm -rf "$exedir"' EXIT
mkexe() {
  local compiler=$1 src=$2
  [ -f "$src" ] && command -v "$compiler" >/dev/null 2>&1 || [ -x "$compiler" ] || return 0
  [ -f "$src" ] || return 0
  cp "$src" "$exedir/" && "$compiler" --compile=exe "$exedir/$(basename "$src")" >/dev/null 2>&1 \
    && echo "$exedir/$(basename "$src").out"
}
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

cols="| %-13s | %-8s | %-14s | %-18s | %-18s | %-18s | %-18s | %-18s | %-18s |\n"
printf "$cols" program "pn" "pn exe (x pn)" "pl p2 (x perl)" "pl perl" "pl exe (x perl)" \
  "p6 p2 (x raku)" "p6 raku" "p6 exe (x raku)"
printf "|%s|%s|%s|%s|%s|%s|%s|%s|%s|\n" "$(printf -- '-%.0s' {1..15})" "$(printf -- '-%.0s' {1..10})" \
  "$(printf -- '-%.0s' {1..16})" "$(printf -- '-%.0s' {1..20})" "$(printf -- '-%.0s' {1..20})" \
  "$(printf -- '-%.0s' {1..20})" "$(printf -- '-%.0s' {1..20})" "$(printf -- '-%.0s' {1..20})" "$(printf -- '-%.0s' {1..20})"
for n in "${names[@]}"; do
  pnexe=-; plexe=-; p6exe=-
  if [[ $n == *.p2 ]]; then
    base=${n%.p2}
    pn=$(timeit "example/$base.pn" "$POTION" "example/$base.pn")
    plp2=$(timeit "example/$n" "$P2" "example/$n")
    plperl=$(timeit "example/$n" "$PERL" "example/$n")
    p6p2=-; p6raku=-
    x=$(mkexe "$POTION" "example/$base.pn"); [ -n "$x" ] && pnexe=$(timeit "$x" "$x")
    x=$(mkexe "$P2" "example/$n"); [ -n "$x" ] && plexe=$(timeit "$x" "$x")
  else
    pn=$(timeit "example/$n.pn" "$POTION" "example/$n.pn")
    plp2=$(timeit "example/$n.pl" "$P2" "example/$n.pl")
    plperl=$(timeit "example/$n.pl" "$PERL" "example/$n.pl")
    p6p2=$(timeit "example/$n.p6" "$P2" "example/$n.p6")
    p6raku=$(timeit "example/$n.p6" "$RAKU" "example/$n.p6")
    x=$(mkexe "$POTION" "example/$n.pn"); [ -n "$x" ] && pnexe=$(timeit "$x" "$x")
    x=$(mkexe "$P2" "example/$n.pl"); [ -n "$x" ] && plexe=$(timeit "$x" "$x")
    x=$(mkexe "$P2" "example/$n.p6"); [ -n "$x" ] && p6exe=$(timeit "$x" "$x")
  fi
  printf "$cols" "$n" "$pn" "$(rel "$pnexe" "$pn")" "$(rel "$plp2" "$plperl")" "$plperl" \
    "$(rel "$plexe" "$plperl")" "$(rel "$p6p2" "$p6raku")" "$p6raku" "$(rel "$p6exe" "$p6raku")"
done
