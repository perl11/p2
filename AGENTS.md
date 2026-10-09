# AGENTS.md — p6/pvip and p5/P2-mode work notes

Guidance for continuing work on this repo's two spec-test corpora:
`test/roast6/` (915-file Raku/p6 spec tests, see the "p6/roast6" section)
and `test/roast5/` (482-file plain-Perl-5 spec tests, native p5/P2 mode, see
the "p5/roast5" section below it). TODO items only — for what's already
been fixed, read `git log`, not this file.

Run roast5 files via `test/roast5.sh [-e bin/p2] test/roast5/op/foo.t` (it runs from inside
test/roast5 and links the p2-repo `test/p5/test.pl`, which the files `require "./test.pl"`).
`prove` works (`TAP::Harness v3.52`, confirmed via `prove --version`) — use
it for real pass/fail counts instead of the exit-code-only scanners below
where convenient, e.g. `prove -e './bin/p2' test/roast5/base/`. The
exit-code scanners are still useful for fast corpus-wide triage sweeps
(finding *which* files to bisect) since `prove` runs serially and is much
slower across 482/915 files; `xargs -P` parallelism matters at this scale.
- `front/p2.c`'s exec-mode bugs (fixed this session, see `git log`) mean
  exit-code scans taken BEFORE that fix undercounted real failures:
  runtime errors (not just parse errors) are now correctly propagated as
  a non-zero exit code where they used to be silently swallowed (JIT
  mode discarded the executed result, always returning the compiled
  Proto to the caller regardless of what actually happened at runtime).
  If your scan's nonzero-exit count looks substantially different from
  a prior session's, check whether both were taken on the same side of
  that fix before assuming a regression — diff the actual per-file
  pass/fail set, not just the aggregate count.

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
  itself changed; gate on `make test.p6 && make test.p2 && make test.p5`).
- `test/runtests.sh` had a long-standing bug (since `d4522c3`, an
  earlier p6-session commit, predates all p5/roast5 work): the
  `look=` variable — meant to hold each `.pn`/`.pl` file's `#=>
  EXPECTED` comment, extracted via `sed` — was accidentally deleted
  rather than kept alongside a new `case $f in */p6/*) continue ;;`
  line added in the same hunk, leaving `look` permanently unset/empty
  for the rest of the script's life. Every `test.pn`/`test.p2`
  comparison silently became `"" != "$actual"`, so EVERY test
  "failed" with the exact same uninformative `expected <>, but got
  <...>` message — fixed by restoring the missing `look=` line.
  Consequence for this document: the previously-recorded `test.p2`
  "4 pre-existing failures" baseline was itself a symptom of this bug
  (comparing "nothing" against output, not a real pass/fail signal) —
  diffing the FULL failure text against a `git stash` baseline (as
  this document instructed) still correctly caught zero regressions
  throughout the p5/roast5 session despite the broken baseline, since
  that method only needs the comparison to be *consistent*, not
  *correct*. With `look=` restored, test.p2's real count is ~37
  failures, ALL in the native (non-p5, non-p6) `syn/syntax.y`-backed
  `test/*.pl`/`test/*.pn` suite — confirmed pre-existing and unrelated
  to any p5/roast5 commit (that grammar file was never touched this
  session). Not triaged further here; a `syntax.y`-focused
  investigation is its own separate body of work, out of scope for
  p5/roast5.
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

- **Regex follow-ups after the PCRE2 matcher integration:** p5
  `=~`/`!~` with `/pat/imsx`, `qr//` (a `(?flags)pat` string, usable as
  `=~ $re`), `s/pat/repl/[gimsx]` (as `$s = $s->subst(...)`; the
  replacement is literal text with PCRE2 `$1`/`${1}` syntax, no Perl
  variable interpolation, no `/e`, expression value is the new string
  not the count) and the match variables `$&`, `` $` ``, `$'`, `$1`..
  work (also inside `"..."`). Still unwired: `m//` and other delimiters,
  `/g` in list/scalar context (`while (/x/g)`, `pos`), `tr///`, `@-`/`@+`,
  `%+`, `$/`, interpolated variables inside the pattern (`/$X[-1]/`),
  `${^OPEN}` (comp/require.t). Gotcha: the match variables are lobby
  globals pre-created in `potion_regex_init` ($1-$9); creating a NEW
  lobby global at runtime (e.g. >9 groups) made already-compiled JIT
  code read stale values (bytecode `-B` was fine) — not root-caused.

- **`@_` limits (design: see `p5_sub_proto` in `syn/syntax-p5.y`).** Plain
  subs whose body mentions `@_` get 12 hidden optional params `$__aN`
  (default sentinel `PN_P5NOARG`) and a prologue `@_ = p5args((...))`
  (`core/table.c`). Consequences: >12 args are dropped; `scalar(@_)`,
  `@_` aliasing and `&f;` are not implemented; `sub f(@r)` still binds
  only one scalar; such programs are forced onto the bytecode VM because
  the x86 JIT fills call-site defaults from `protos[0]` only (see
  `potion_x86_call`). A real fix is varargs in the sig/VM/JIT arg passing.

- **`test/p5/test.pl` is a small p2-subset TAP library** (plan, ok, is,
  isnt, pass, fail, skip, skip_all, diag, note, done_testing/done), not
  perl's real `t/test.pl`, which p2 cannot parse yet (about 50 of 88
  top-level chunks fail: sub prototypes `sub f ($$)`, `map`/`grep` blocks,
  `local`, `wantarray`, `//`, `@{[...]}`, `qx`). `require "file"` is
  expanded at *parse time* (`p5_require`), a BEGIN block containing one
  stays in the program. Known limits: no `like`/`is_deeply`/`cmp_ok`,
  `skip()` cannot leave its SKIP block, a sub calling another sub that is
  defined LATER in the file does nothing (forward references), and
  `last`/`next` outside a lexical loop is a compile error. Run roast5 with
  `test/roast5.sh -e bin/p2 <dir-or-files>`; `make test.p2` runs `OK_ROAST`.

- **`(EXPR)` is still parsed as a one-element list literal** everywhere
  except two scalar contexts: the rhs of a scalar assignment and a ternary
  condition (`p5_unparen` in `syn/syntax-p5.y` unwraps them). Other
  places that want a scalar (`&&`/`||` operands, function args,
  `return (x == y)`, comparison operands) still see a tuple, which is
  always truthy. A real fix needs scalar-vs-list context threading.
- **References are unsupported**: `$h->{k}`, `$r->[i]` are parse errors
  (`method` needs a method name after `->`); anon `{...}`/`[...]`
  constructors are untested. Likely the largest remaining blocker in
  roast5. `my @a = <foo>` (readline) also fails to parse.

- **Coderef call gaps**: `$cb->(args)` works (and `shift`/`$_[N]` inside
  the closure bind via `@_`), but
  chained `$f->(1)->(2)` / `$h{cb}->()` / `$cb->call()` (closures have no
  `call` method) are unsupported — `p5coderef` only takes a plain
  scalar on the left of `->(`.

- **`delete $arr[$i]` and `delete $href->{k}` are still silent no-ops** —
  only the `delete $h{key}` form has a grammar alternative (`p5delete`,
  sends the table `delete` method). Array elements need a
  non-copying tuple remove; the arrow form needs the `methlhs` chain.
- Beyond that, the remaining majority of failing files are
  architecturally the same situation as roast6's parse-error bucket: a
  long tail of individual p5-grammar gaps (interpolated regex expressions
  such as `/$X[-1]/` are now the first parser blocker in `base/lex.t`;
  heredocs embedded inside interpolated quotes/regex constructs remain
  unsupported). Use the TAP-scanner +
  stderr-bucketing approach above to find the next highest-frequency one
  rather than guessing.

## Process notes for whoever continues this

- Foreach new class or method, ensure test coverage for potion, p5 or p6 tests.
- Commit granularly, one root-cause per commit, with the bisection repro
  and the *why* (not just the diff) in the commit message — `git log` is
  where that history lives, not this file. Keep this file to open TODOs
  only; delete an entry here the moment it's fixed (move any useful
  "gotcha" detail into the commit message instead, or into a code
  comment if it'll trip up the next edit).
- Always re-verify the full set of previously-fixed minimal repros (keep
  them around in `/tmp/`, they're one-liners) plus `make test.p6 test.p2
  test.p5` before committing.
- Bundle any AGENTS.md edits into the SAME commit as the code/test fix
  they relate to — don't follow up with a separate docs-only commit.
- gortex:
@CLAUDE.md
