#!/bin/sh
# End-to-end C backend tests. Run via make test.compile-c, or after make bins libs.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"
MAKE=${MAKE:-make}
cc=${POTION_CC:-${CC:-}}
if [ -z "$cc" ]; then
    cc=$($MAKE --no-print-directory -s -f config.inc -f - <<'MAKE_EOF'
print-compile-c-cc:
	@printf '%s\n' '$(CC)'
MAKE_EOF
    )
fi
loadext=${POTION_LOADEXT:-.so}
tmp=$(mktemp -d "${TMPDIR:-/tmp}/potion-compile-c.XXXXXXXX")
trap 'rm -rf "$tmp"' EXIT
trap 'exit 1' HUP INT TERM
export LD_LIBRARY_PATH="$root/lib:$root/lib/p2${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export DYLD_LIBRARY_PATH="$root/lib:$root/lib/p2${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"

# Compiler/flag variables deliberately allow a command with arguments (e.g. ccache cc).
compile() {
    runtime=$1
    shift
    case $runtime in
        potion) set -- "$@" -L"$root/lib" -lpotion ;;
        p5) set -- "$@" -L"$root/lib/p2" -lp5 -L"$root/lib" -lp2 ;;
        p6) set -- "$@" -L"$root/lib/p2" -lp6 -lp5 -L"$root/lib" -lp2 ;;
    esac
    $cc ${CPPFLAGS:-} ${POTION_CFLAGS:-${CFLAGS:-}} -I"$root/core" \
        ${POTION_LDFLAGS:-${LDFLAGS:-}} "$@" \
        -Wl,-rpath,"$root/lib" -Wl,-rpath,"$root/lib/p2" -lm
}

check() {
    name=$1
    expected=$2
    shift 2
    "$@" > "$tmp/actual"
    if ! diff -u "$expected" "$tmp/actual"; then
        printf 'FAIL: %s\n' "$name" >&2
        exit 1
    fi
    printf 'ok - %s\n' "$name"
}

cat > "$tmp/potion.pn" <<'PN_EOF'
n = 1
bump = (delta:=2): n = n + delta.
n = 10
bump() print
"\n" print
bump(5) print
"\n" print
factory = (x):
  (delta:=1): x = x + delta.
.
counter = factory(20)
counter() print
"\n" print
counter(3) print
"\n" print
i = 0
sum = 0
while (i < 5):
  if (i == 2): sum = sum + 10. else: sum = sum + i.
  i = i + 1.
sum print
"\n" print
9007199254740991 print
"\n" print
1.25 print
"\n" print
"quote=\" slash=\\\nline\tend\n" print
argv(1) print
"\n" print
argv(2) print
"\n" print
PN_EOF
printf '12\n17\n21\n24\n18\n9007199254740991\n1.25\nquote=" slash=\\\nline\tend\nfirst arg\nsecond\n' > "$tmp/potion.expected"
bin/potion --compile=c "$tmp/potion.pn"
compile potion "$tmp/potion.pn.c" -o "$tmp/potion"
check 'Potion closures, defaults, loops, constants, escaping and argv' \
    "$tmp/potion.expected" "$tmp/potion" 'first arg' second

cat > "$tmp/contracts.pn" <<'PN_EOF'
pair = (x:=10, y:=20): (x,y).
pair(y=nil) string print
"\n" print
wide = (a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p): a + p.
wide(1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16) print
"\n" print
Point = class(x): /x = x.
Point value =: /x.
Point(9) value print
"\n" print
(9007199254740992 < 9007199254740993, 9007199254740993 <= 9007199254740992,
 9007199254740993 > 9007199254740992, 9007199254740992 >= 9007199254740993) string print
"\n" print
PN_EOF
printf '(10, nil)\n17\n9\n(true, false, true, false)\n' > "$tmp/contracts.expected"
bin/potion --compile=c "$tmp/contracts.pn"
compile potion -O2 "$tmp/contracts.pn.c" -o "$tmp/contracts"
check 'named defaults, wide calls, classes and exact integer comparisons' \
    "$tmp/contracts.expected" "$tmp/contracts"

# Explicit archive linking catches dependencies absent from the shared-library build.
$cc ${CPPFLAGS:-} ${POTION_CFLAGS:-${CFLAGS:-}} -I"$root/core" \
    "$tmp/contracts.pn.c" "$root/lib/libpotion.a" \
    "$root/3rd/pcre/.libs/libpcre2-8.a" -lm -ldl -lpthread -o "$tmp/static-runtime"
check 'static Potion runtime' "$tmp/contracts.expected" "$tmp/static-runtime"

cat > "$tmp/p5.pl" <<'P5_EOF'
my $n = 1;
sub bump($delta=2) { $n = $n + $delta; }
$n = 10;
say bump();
say bump(5);
my $i = 0;
my $sum = 0;
while ($i < 5) {
    if ($i == 2) { $sum = $sum + 10; }
    else { $sum = $sum + $i; }
    $i = $i + 1;
}
say $sum;
say 7 + 3 * 4;
say 2 ** 5;
say 1.25 + 0.5;
say "quote=\" slash=\\";
say scalar(@ARGV);
say $ARGV[0];
say $ARGV[1];
P5_EOF
cat > "$tmp/p5.expected" <<'EXPECTED'
12
17
18
19
32
1.75
quote=" slash=\
2
first arg
second
EXPECTED
bin/p2 --compile=c "$tmp/p5.pl"
compile p5 "$tmp/p5.pl.c" -o "$tmp/p5"
check 'p5 runtime, captures, defaults, control flow, arithmetic and argv' \
    "$tmp/p5.expected" "$tmp/p5" 'first arg' second

cat > "$tmp/p6.p6" <<'P6_EOF'
use v6;
say (1 + 2, 10 - 3, 3 * 4, 10 / 2, 2 ** 3, 10 % 3, 1 + 2 * 3).join(" ");
say 123456789012345678901234567890 + 1;
say "p6 runtime";
P6_EOF
printf '3 7 12 5 8 1 7\n123456789012345678901234567891\np6 runtime\n' > "$tmp/p6.expected"
bin/p2 --compile=c "$tmp/p6.p6"
compile p6 "$tmp/p6.p6.c" -o "$tmp/p6"
check 'p6 arithmetic and runtime initialization' "$tmp/p6.expected" "$tmp/p6"

# A real loader call checks filename-derived initializer lookup and execution.
# The separately exported run function must also preserve the program's return value.
cat > "$tmp/module.pn" <<'PN_EOF'
"module initialized\n" print
40 + 2
PN_EOF
bin/potion --compile=c "$tmp/module.pn"
compile potion -DPOTION_MODULE -shared -fPIC "$tmp/module.pn.c" -o "$tmp/module$loadext"
compile potion -DPOTION_MODULE -DPOTION_MODULE_NAME=Renamed -shared -fPIC \
    "$tmp/module.pn.c" -o "$tmp/Renamed$loadext"
cat > "$tmp/module-check.c" <<'C_EOF'
#include <stdio.h>
#include <dlfcn.h>
#include "p2.h"

int main(int argc, char **argv) {
    POTION_INIT_STACK(sp);
    Potion *P = potion_create(sp);
    void *handle;
    PN (*run)(Potion *);
    PN value;
    if (argc != 2) return 1;
    value = potion_load(P, PN_NIL, PN_NIL, potion_str(P, argv[1]));
    if (value != PN_TRUE) {
        fprintf(stderr, "generated module failed to load: %s\n", argv[1]);
        return 1;
    }
    handle = dlopen(argv[1], RTLD_NOW);
    if (!handle) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    run = (PN (*)(Potion *))dlsym(handle, "potion_compiled_run");
    if (!run) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    value = run(P);
    if (value != PN_NUM(42)) {
        fprintf(stderr, "generated module did not return 42\n");
        return 1;
    }
    potion_destroy(P);
    dlclose(handle);
    puts("module returned 42");
    return 0;
}
C_EOF
compile potion "$tmp/module-check.c" -ldl -o "$tmp/module-check"
printf 'module initialized\nmodule initialized\nmodule returned 42\n' > "$tmp/module.expected"
check 'shared module loader and returned value' "$tmp/module.expected" \
    "$tmp/module-check" "$tmp/module$loadext"
check 'shared module initializer override' "$tmp/module.expected" \
    "$tmp/module-check" "$tmp/Renamed$loadext"

# A hyphen in the original source name must produce a valid initializer token.
cp "$tmp/module.pn" "$tmp/odd-name.pn"
bin/potion --compile=c "$tmp/odd-name.pn"
compile potion -DPOTION_MODULE -shared -fPIC "$tmp/odd-name.pn.c" -o "$tmp/odd_name$loadext"
check 'sanitized source basename initializer' "$tmp/module.expected" \
    "$tmp/module-check" "$tmp/odd_name$loadext"

# Generated p5/p6 modules must initialize their runtimes in a plain embedding host.
for runtime in p5 p6; do
    if [ "$runtime" = p5 ]; then
        printf 'say "module initialized"; 40 + 2;\n' > "$tmp/lang.pl"
        src="$tmp/lang.pl"
    else
        printf 'use v6; say "module initialized"; 40 + 2;\n' > "$tmp/lang.p6"
        src="$tmp/lang.p6"
    fi
    bin/p2 --compile=c "$src"
    compile "$runtime" -DPOTION_MODULE -shared -fPIC "$src.c" -o "$tmp/lang$loadext"
    compile "$runtime" -DP2 "$tmp/module-check.c" -ldl -o "$tmp/lang-check"
    check "$runtime shared module runtime initialization" "$tmp/module.expected" \
        "$tmp/lang-check" "$tmp/lang$loadext"
done

# A compiled closure's exposed prototype remains callable through the public API.
printf 'callback = (x): x + 2.\ncallback\n' > "$tmp/callback.pn"
bin/potion --compile=c "$tmp/callback.pn"
compile potion -DPOTION_MODULE -shared -fPIC "$tmp/callback.pn.c" -o "$tmp/callback$loadext"
cat > "$tmp/callback-check.c" <<'C_EOF'
#include <dlfcn.h>
#include <stdio.h>
#include "potion.h"
int main(int argc, char **argv) {
    POTION_INIT_STACK(sp);
    Potion *P = potion_create(sp);
    void *handle = dlopen(argv[1], RTLD_NOW);
    if (!handle) return 1;
    PN (*run)(Potion *) = (PN (*)(Potion *))dlsym(handle, "potion_compiled_run");
    if (!run) return 1;
    PN closure = run(P);
    PN proto = potion_send(closure, potion_str(P, "code"));
    PN args = potion_tuple_new(P, PN_NUM(40));
    potion_garbagecollect(P, 0, 1);
    if (potion_send(proto, PN_call, args) != PN_NUM(42)) return 1;
    if (potion_vm(P, proto, P->lobby, args, 0, NULL) != PN_NUM(42)) return 1;
    PN bytes = potion_source_dumpbc(P, PN_NIL, proto, PN_NIL);
    if (PN_TYPE(bytes) != PN_TERROR) return 1;
    potion_destroy(P);
    dlclose(handle);
    puts("prototype returned 42 after collection");
    return 0;
}
C_EOF
compile potion "$tmp/callback-check.c" -ldl -o "$tmp/callback-check"
printf 'prototype returned 42 after collection\n' > "$tmp/callback.expected"
check 'compiled prototype calls, collection and bytecode rejection' \
    "$tmp/callback.expected" "$tmp/callback-check" "$tmp/callback$loadext"
