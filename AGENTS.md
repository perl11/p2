# AGENTS.md — p6/pvip and p5/P2-mode work notes

Guidance for continuing work on this repo's two spec-test corpora:
`test/roast6/` (915-file Raku/p6 spec tests, see the "p6/roast6" section)
and `test/roast5/` (482-file plain-Perl-5 spec tests, native p5/P2 mode, see
the "p5/roast5" section below it). TODO items only — for what's already
been fixed, read `git log`, not this file.

`prove` works (`TAP::Harness v3.52`, confirmed via `prove --version`) — use
it for real pass/fail counts instead of the exit-code-only scanners below
where convenient, e.g. `prove -e './bin/p2' test/roast5/base/`. The
exit-code scanners are still useful for fast corpus-wide triage sweeps
(finding *which* files to bisect) since `prove` runs serially and is much
slower across 482/915 files; `xargs -P` parallelism matters at this scale.

## p6/roast6

### How to triage efficiently (read before bisecting by hand)

The naive "split file on blank lines, add segments one at a time" bisector
**gives false failures** on any file containing a `=begin pod ... =end pod`
block (pod bodies contain blank lines) or any construct with a blank line
inside balanced braces. Use a brace-depth + pod-aware segmenter instead:

```python
def bisect(path):
    lines = open(path).readlines()
    segs, cur, depth, in_pod = [], [], 0, False
    for line in lines:
        cur.append(line)
        s = line.strip()
        if s.startswith('=begin'): in_pod = True
        if s.startswith('=end'): in_pod = False
        if not in_pod: depth += line.count('{') - line.count('}')
        if depth == 0 and not in_pod and s == '':
            segs.append(cur); cur = []
    if cur: segs.append(cur)
    acc = ""
    for i, seg in enumerate(segs):
        acc += "".join(seg)
        open('/tmp/bsect.pl','w').write(acc)
        r = subprocess.run(['./bin/p2','-6','/tmp/bsect.pl'],
                            cwd='/home/rurban/Software/p2',
                            capture_output=True, timeout=3, text=True)
        if r.returncode != 0:
            print(f"FAIL at segment {i} (rc={r.returncode})"); print("".join(seg)); return
    print("all segments ok")
```

Run it via `eval(language="py", ...)`. The error message printed by
`syntax_parse()` on a real parse failure (`p6 parse error: Syntax error!
Around: <dump of first ~N bytes of the WHOLE file>`) is **useless for
localizing the failure** — it always shows roughly the file start
regardless of where parsing actually broke. Don't trust it; bisect instead.

For crashes, `gdb -batch -ex run -ex "bt 8" --args ./bin/p2 -6 file.pl`
almost always lands either in `pvip_to_pn()` (translator bug — check the
grammar rule's actual `PVIP_node_new_childrenN(...)` call in
`syn/syntax-p6.y` against what the translator's `case` assumes about child
count/order) or in `core/compile.c`'s `potion_source_asmb`/
`potion_sig_compile` (AST shape doesn't match what the compiler expects —
cross-reference against how `syntax-p5.y` builds the equivalent AST, since
that's the "known good" producer compile.c was written against).

**Recurring bug pattern, check for it reflexively**: `PVIPNode` is a
tagged union (`pvip.h`): `->pv` (string-leaf nodes, from
`PVIP_node_new_string`) and `->children` (branch nodes, from
`PVIP_node_new_childrenN`) occupy the *same memory*. Reading `->pv` on a
children-node (or vice versa) is a garbage-pointer deref, not a safe
NULL — it crashes, often confusingly deep in unrelated code. Any time you
add/touch a `pvip_to_pn.c` case, check `syntax-p6.y` for how that node
type is actually constructed before assuming `->pv` or `->children` is
valid.

Corpus-wide sweep (run after any batch of fixes to measure impact):

```bash
find test/roast6 -name '*.t' | xargs -P 16 -I{} sh -c '
  out=$(timeout 2 ./bin/p2 "{}" 2>&1 >/dev/null); rc=$?
  if [ $rc -eq 124 ]; then echo "TIMEOUT {}";
  elif [ $rc -ne 0 ]; then echo "$out" | head -1; fi
' > /tmp/roast6_errs.txt
wc -l /tmp/roast6_errs.txt   # nonzero-exit count
```

**Caveat**: this only counts process exit code. A file that parses and runs
but prints wrong values (`not ok N` in TAP) still exits 0 and is invisible
to this metric — it undercounts remaining semantic bugs. Prefer `prove`
(see top of file) for a real count when feasible.

### TODO (in rough priority order)

- **Object instantiation / method dispatch on instances doesn't work.**
  `class Foo { method greet(){say "hi"} }; Foo.new; $f.greet;` doesn't
  crash but `.greet` silently produces no output — the constructor/
  instance/method-call runtime protocol isn't wired up. Given how central
  classes are to the corpus, probably the single highest-value next
  target, but it's a real design task (how does `OP_CLASS` + attribute
  storage + method resolution actually work in the existing p2 VM?), not
  a quick fix.
- **The large "parse error" bucket has no single common cause** — it's a
  long tail of individual advanced constructs. Confirmed NOT "parenless
  Test.pm calls" (those work fine standalone). Known constructs seen
  during sampling that don't parse:
  - `regex NAME { ... }` / `rule NAME { ... }` / `token` (grammars)
  - `sub infix:<op>(...)  {...}` / `prefix:<op>` / custom operator defs
  - sigilless params `sub f(\x) { x }`
  - multiple dispatch signatures `multi sub f(A $x) | (B $x) {...}`
  - `$?LINE`, `$?MODULE`, other compile-time "magic" variables (some are
    stubbed in libp6, not all)
  - hash slices `%h{1,3,5}`, `%h<1 3 5>`
  - `(42 unless $cond)` / ternary `??  !!` used as an *expression* inside
    a list literal
  - heredocs (`q<<<...`), POD edge cases
  - `goto`, sigilless returns-type `returns Foo`
  Pick one at a time via the sampling+bisect loop above; don't try to
  find "the" fix, there isn't one.
- **Several `** Invalid tuple cmp type` fatals** (`core/table.c:807`,
  `potion_fatal`) — e.g. `isa_ok (5,7,8), Parcel, ...`. Not yet traced to
  a specific pvip_to_pn.c case.
- **"unhandled node type N" pops up after each round of fixes** (different
  N each time) — the default case in `pvip_to_pn()`'s switch still returns
  raw `PN_NIL`, which is itself latently crash-prone (NULL used as a tree
  node). Consider hardening the `default:` case itself to return a safe
  no-op `EXPR(SRC(VALUE, PN_NIL))` instead of bare `PN_NIL`, as a blanket
  defense — would turn future "forgot to implement node type X" bugs into
  silent no-ops instead of crashes. Tradeoff: might mask bugs that are
  better left loud. Flagged for a decision, not just "do it".
- **Several files with 2-second TIMEOUTs** (`while`/`until`/regex-heavy
  files) — likely infinite loops from mistranslated loop conditions or
  runtime stubs that don't actually terminate iteration. Not triaged.
- **`PVIP_NODE_PERL`** node type is entirely unhandled (falls to
  `default:`, returns bare `PN_NIL`). `perl:5<...>`-style embedding, rare.
- **TODO**: wire up a real `test.roast6` Makefile target using `prove`
  for real pass/fail counts, not just exit-code.

## p5/roast5

### Orientation

- No `-6`/mode-switch involved: `test/roast5/` is plain Perl 5
  (`#!./perl` shebang), runs through the native `syn/syntax-p5.y` grammar
  and `core/compile.c`, no `pvip`/p6 machinery at all.
- Same build/regression commands as the p6/roast6 section above
  (`make -j8 bin/p2`, needs `make syn/syntax-p5.c` first if `syntax-p5.y`
  itself changed; gate on `make test.p6 && make test.p2`).
- TAP-aware scanner (better than roast6's exit-code-only sweep — counts
  actual `ok`/`not ok` lines against the `1..N` plan, so it can tell "ran
  clean but got wrong answers" apart from "crashed/didn't parse"):

```python
import subprocess, glob, re
def tap_score(stdout):
    planned = None; passed = failed = 0
    for line in stdout.splitlines():
        m = re.match(r'1\.\.(\d+)', line.strip())
        if m: planned = int(m.group(1)); continue
        m = re.match(r'(not )?ok\b', line.strip())
        if m:
            if m.group(1): failed += 1
            else: passed += 1
    return planned, passed, failed

results = []
for f in sorted(glob.glob('test/roast5/**/*.t', recursive=True)):
    try:
        r = subprocess.run(['./bin/p2', f], capture_output=True, timeout=3, text=True)
        rc = r.returncode
    except subprocess.TimeoutExpired:
        rc = 'TIMEOUT'; r = None
    planned, passed, failed = tap_score(r.stdout) if r else (None, 0, 0)
    results.append((f, rc, planned, passed, failed, r.stderr[:200] if r else ''))
```

Bucket `results` by normalized `stderr` (strip quoted literals/line numbers
via regex) to find the highest-frequency distinct failure, same idea as the
roast6 bisector above — just run it directly instead of the brace-depth
bisector (roast5 files are much shorter and p5 error messages, while still
imprecise about *location*, at least correctly distinguish "didn't parse"
(`Syntax error near token 'X' before text "Y"` / `Internal parser error:
Couldn't parse all statements before text "Y"`) from real runtime bugs).

### TODO

- **`=~` (regex match) operator entirely unsupported** — no grammar rule
  at all, `$s =~ /pattern/;` fails to parse even standalone. Blocks
  `test/roast5/comp/require.t` and almost certainly the bulk of
  `test/roast5/re/` (50 files) plus anything using pattern matching
  anywhere. `${^OPEN}` (a special all-caps-braced variable form) is also
  unhandled. The p6/roast6 side's regex support is ALSO just a stub
  (`p6_call_str(..., "p6_regexp")` → "not yet implemented"), so real
  regex engine support doesn't exist in this codebase at all yet for
  either mode — this is a substantial, multi-file feature (lexer for
  `/pattern/flags`, an actual matching engine or libc regex/PCRE bridge,
  capture-group variables `$1`/`$&`/`%-`/`$/`), not a quick grammar
  patch. Highest-value next target by file-count impact, but sized as
  its own session, not a bisect-and-one-line-fix item.
- **`abs($x)`/`chr($x)` (parens call form) silently return `undef`;
  `abs $x`/`chr $x` (bareword named-unary, no parens) work correctly**
  — these are registered as 0-arg METHODS on number vtables
  (`core/number.c`: `potion_method(num_vt, "abs", potion_num_abs, 0)`),
  not free functions. Traced the AST-shape difference precisely: the
  bareword form (`c:call e:expr` in `expr`) builds `PN_PUSH(PN_TUPIF(e),
  msg)` — an EXPR tuple `[value, msg]`, which p2's EXPR-sequencing
  evaluates as "compute value, then send msg to it" — i.e. effectively
  `$x.abs`, which correctly resolves to the method. The parenthesized
  form (`calllist`'s `m:name - list-start l:callexprs list-end -`)
  builds `PN_TUP(m)` — just the msg alone, sent to the default/implicit
  self (lobby), where `abs` isn't defined → undef, no error. Not a
  quick fix: making `calllist` ALSO self-chain for single-arg calls
  would silently change semantics for user-defined functions too
  (`sub foo {...}; foo(5)` would become `5.foo` instead of a lobby-level
  call) — needs either a per-name whitelist of known unary-method
  builtins or a runtime method-missing fallback on the lobby, not
  attempted here.
- **JIT miscompiles comparisons (`<=`, confirmed; others not yet
  checked) against `undef`/NIL — crashes, bytecode VM doesn't.**
  `my $x; say($x <= 3);` segfaults with the default JIT execution
  mode; identical script runs fine (wrong-but-non-crashing output)
  under `./bin/p2 -B` (bytecode VM) — real Perl numifies `undef` to 0
  in comparisons instead of crashing. Found via the `abs($x)` gap
  above feeding undef into `<=` in num.t's `_ok()` helper. Not triaged
  into the JIT codegen (`core/vm-x86.c` presumably) — gdb backtraces
  on the JIT path are unsymbolized (JIT-generated machine code), would
  need a different debugging approach (disassembly of the generated
  code, or adding JIT debug tracing) than anything used elsewhere in
  this file.
- **`(EXPR)` is always parsed as a list-literal, never pure grouping
  parens** — `my $x = (1 == 2);` assigns a 1-element TUPLE containing
  the boolean, not the boolean itself; since tuples are always truthy as
  VM register objects, any later boolean test of that value (e.g. a
  ternary condition written with habitual/defensive parens, `(cond) ? a
  : b`) is ALWAYS true regardless of `cond`. Real Perl disambiguates
  grouping-parens from list-constructor-parens by context (scalar vs
  list); this grammar doesn't. Broad, affects far more than ternary. Not
  attempted: real fix likely needs scalar-vs-list context threading
  through `assigndecl`/`list`, a bigger grammar change than a
  single-session fix.
- **`my @arr = <single-quoted string>` and `my @arr = qw(words with
  spaces)` still fail to parse** (everything else about qw and array
  decl works: scalars, double-quoted strings, numbers, barewords,
  paren-lists, all qw delimiter forms in scalar/expr context). Traced
  extensively with -Dp: the statement loses to a bareword-call parse of
  'my' (via `sets sep?` where sep is optional), and with that path
  blocked (my/our/local added to `keyword`) assigndecl still genuinely
  fails, pointing at greg's backtracker/memoization corrupting state
  across the failed `assigndecl IF` stmt alternative rather than at
  grammar coverage — the identical grammar paths succeed for `$`-sigil
  LHS. Two abandoned fix attempts: reordering `assigndecl` broke
  `my @f = <anything>` wholesale; adding my/our/local to `keyword`
  regressed `my sub cl4 {}` in test.p2 (`lexsubrout` is commented out,
  so `my sub` currently parses via the call path that the keyword change
  blocked). Whoever picks this up: the `my sub` dependency means any
  keyword-type fix must first implement a real `MY SUB` grammar
  alternative.
- Beyond that, the remaining majority of failing files are
  architecturally the same situation as roast6's parse-error bucket: a
  long tail of individual p5-grammar gaps (heredocs confirmed in
  `base/lex.t`; others not yet sampled). Use the TAP-scanner +
  stderr-bucketing approach above to find the next highest-frequency one
  rather than guessing.

## Process notes for whoever continues this

- Commit granularly, one root-cause per commit, with the bisection repro
  and the *why* (not just the diff) in the commit message — `git log` is
  where that history lives, not this file. Keep this file to open TODOs
  only; delete an entry here the moment it's fixed (move any useful
  "gotcha" detail into the commit message instead, or into a code
  comment if it'll trip up the next edit).
- Always re-verify the full set of previously-fixed minimal repros (keep
  them around in `/tmp/`, they're one-liners) plus `make test.p6 test.p2`
  before committing — this codebase has no CI, regressions are silent
  otherwise.
- `commit-priv` skill applies to this repo (`github.com/perl11/p2`, no
  `/SpexAI/` in the remote): `--author "Reini Urban
  <reini.urban@gmail.com>"` and `--date` outside 08:00–17:00.
- Bundle any AGENTS.md edits into the SAME commit as the code/test fix
  they relate to — don't follow up with a separate docs-only commit.
