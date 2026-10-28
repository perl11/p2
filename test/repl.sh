#!/bin/sh
# The variables of the p2 -d loop persist from one line to the next (doc/debugger.md).
# The loop needs a terminal: the lines are typed through util-linux script(1), skipped elsewhere.
cd "$(dirname "$0")/.." || exit 1
p2=${P2:-bin/p2}
if ! script --version 2>/dev/null | grep -q util-linux; then
    echo "skip: test/repl.sh needs util-linux script"
    exit 0
fi
tmp=$(mktemp -d "${TMPDIR:-/tmp}/p2-repl.XXXXXX") || exit 1
trap 'rm -rf "$tmp"' EXIT
(
    sleep 1
    for l in '$x = 5' '$x * 2' 'my $y = 10' '$x + $y' '@a = (1,2,3)' 'push @a, 4' 'scalar(@a)' \
             '%h = (k => 7)' '$h{k} + $x' 'sub sq { $_[0]*$_[0] }' 'sq($x)' \
             '$c = sub { $x++ }' '$c->(); $c->()' '$x' 'for my $i (1..3) { $x += $i }' '$x' \
             '$x = 1; push @a, 9; die "boom\n"' '$x' 'scalar(@a)' '$x % 4' \
             '@c = (4,5,6)' 'sub total {' '    my $t = 0;' '    $t += $_ for @c;' '    return $t;' '}' 'total()' \
             'sub addc { push @c, 10; scalar(@c) }' 'addc()' 'scalar(@c)' 'total()' \
             '%h = (a => 1)' 'sub geth { $h{a} }' 'geth()' '$h{a} = 7' 'geth()' \
             '$g = 1' 'sub showg { $g }' 'local $g = 2; showg()' '$g' 'local @c = (1); total()' 'scalar(@c)' \
             'sub bad { 1 +* }' 'total()' 'my @l = (1,' '2,3)' 'scalar(@l)' 'q'; do
        printf '%s\r' "$l"
        sleep 0.3
    done
) | (cd "$tmp" && script -qec "$OLDPWD/$p2 -d" /dev/null) | tr -d '\r' > "$tmp/out"
# the answers, in order: lines that start with "=> " or the die text
grep -E '^(=> |boom$)' "$tmp/out" | sed -e 's/^=> //' | grep -v '^sub(' > "$tmp/got"
cat > "$tmp/want" <<'EOF'
5
10
10
15
(1, 2, 3)
4
4
(k=7)
12
25
6
7
undef
13
boom
1
5
1
(4, 5, 6)
15
4
4
25
(a=1)
1
(a=7)
7
1
2
1
1
4
25
(1, 2, 3)
3
EOF
# (assignment results of the hash and sub lines are not compared: they differ in layout)
if diff -u "$tmp/want" "$tmp/got" > "$tmp/diff"; then
    echo "ok - p2 -d keeps variables, arrays, hashes, subs, local and multi line input between lines"
else
    cat "$tmp/diff"
    echo "FAIL: p2 -d variables"
    exit 1
fi
