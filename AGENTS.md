# AGENTS.md — p6/pvip work notes

Guidance for continuing work on Perl 6 (Raku) support in p2, specifically
getting `test/roast6/` (the 915-file Raku spec-test corpus, vendored from
roast) to parse/run/pass. This file tracks what's fixed, what's known
broken, and how to keep triaging efficiently.

## Quick orientation

- `-6` flag / `use v6;` / `use p6;` pragma / `.t`,`.p6`,`.raku` extensions all
  select **p6 mode**: the whole file is parsed directly by the `pvip`
  PEG grammar (`syn/syntax-p6.y`, generated into `syn/syntax-p6.c`), not
  the native p5/p2 grammar (`syn/syntax-p5.y`).
- `syn/pvip_to_pn.c`'s `pvip_to_pn()` translates pvip's `PVIPNode*` AST into
  p2's native `PNSource` AST, which `core/compile.c` then compiles to
  bytecode. **This translator is where almost all the bugs are** — the
  pvip grammar itself is fairly complete; the gap is translation + a thin
  p6-semantics runtime (`lib/p6/libp6.c`, dlopened as `libp6.so`).
- Build after editing: `make -j8 bin/p2 lib/p2/libsyntax-p6.so lib/p2/libp6.so`
  (editing `syn/syntax-p6.y` itself additionally needs `make syn/syntax-p6.c`
  first, via the `bin/greg` PEG compiler).
- Regression gate before every commit: `make test.p6 && make test.p2`.
  `test.p2` has **4 pre-existing failures unrelated to p6** (`.plc`
  bytecode-loader bugs: `test/base/assign.plc` missing + several
  "Internal parser error... .plc:1" + 2 JIT string-escaping diffs) —
  confirmed present on a clean `d4522c3` checkout via `git stash`, not
  caused by this work. Compare against that baseline, don't chase them.

## How to triage efficiently (read before bisecting by hand)

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
to this metric — it undercounts remaining semantic bugs. There's no `prove`
harness wired up yet for real pass/fail counting (see TODO below).

## Fixed this session (commits on `pvip` branch, chronological)

1. **`32f76a7`** — `PVIP_NODE_USE` returned raw `PN_NIL`(=NULL) as an AST
   node instead of a valid no-op, crashing on any `use`/`use v6;` statement.
   Also: `my ($a,$b) = EXPR` destructuring crashed (no multi-assign in the
   VM; desugared to temp+`p6_atpos`). `for`/`.method` were entirely
   unimplemented (highest-frequency single gap, 43/915 files).
2. **`8e2ebe6`** — sigils were being stripped before building variable
   names, so `my @a` and `my $a` collided into the same local slot
   (p5/p2's own convention keeps the sigil as part of the name — verified
   via `--verbose` disasm showing `.local $x`). Caused stack-overflow
   crashes in `potion_any_cmp` on any file using same-letter `@x`/`$x`.
3. **`a6d7d08`**, **`0551175`** — `PVIP_NODE_FUNC` read the wrong child
   index for the body (grammar has 4 children incl. a return-type slot
   the translator didn't know about) → every named `sub` crashed.
   `PVIP_NODE_PARAM` didn't emit the shape `potion_sig_compile` expects
   (bare sigil-prefixed name string) → params crashed, then (once shape
   was superficially right) silently failed to bind the argument value.
4. **`c9e8370`** — comment-only/empty files crashed (`PN_NIL` proto handed
   straight to `potion_jit_proto` with no NIL check). Fixed at the
   `syntax_parse()` root: wrap any non-`AST_CODE`/`AST_BLOCK` translation
   result in a `CODE` block before returning.
5. **`548abc4`** — `class Foo { ... }` crashed unconditionally: wrong
   child-index bug (same class as FUNC) plus **every** sub/method using
   `()` empty-parens crashed too (the grammar's `MAYBE(p)` yields a `NOP`
   placeholder when params are absent, and that NOP's translation isn't a
   valid sig shape). Added `pvip_params()` guard, used at all 3 call sites
   (FUNC/LAMBDA/METHOD).
6. **`91b617a`** — the union-aliasing bug pattern described above, found in
   6 places: `$.attr`, regex literals (`m/.../`, `m:P5/.../`), `SLANGS`,
   `PATH`, unicode-char escapes all routed through the children-iterating
   `p6_call()` instead of the string-reading `p6_call_str()` (new helper).
7. **`6686284`** — same union-aliasing bug, 7th instance: slurpy params
   (`sub f(*@args)`) wrap the real var in a `PVIP_NODE_VARGS` *children*
   node; the PARAM-shape fix from commit 4 blindly read `->pv` on it.

**Net measured impact**: corpus-wide nonzero-exit count went from
**787/915 → 677/915** (process-exit-code metric only, see caveat above).

## Known-broken, not yet fixed (in rough priority order)

- **Object instantiation / method dispatch on instances doesn't work.**
  `class Foo { method greet(){say "hi"} }; Foo.new; $f.greet;` no longer
  *crashes* (fixed above) but `.greet` silently produces no output — the
  constructor/instance/method-call runtime protocol isn't wired up.
  Given how central classes are to the corpus, this is probably the
  single highest-value next target, but it's a real design task (how does
  `OP_CLASS` + attribute storage + method resolution actually work in the
  existing p2 VM?), not a one-line fix like the crashes above.
- **The 493-file "parse error" bucket has no single common cause** — it's
  a long tail of individual advanced constructs. Confirmed NOT the
  originally-suspected "parenless Test.pm calls" (those work fine
  standalone). Known constructs seen during sampling that don't parse:
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
- **7 remaining `** Invalid tuple cmp type` fatals** (`core/table.c:807`,
  `potion_fatal`) — e.g. `isa_ok (5,7,8), Parcel, ...`. Not yet traced to
  a specific pvip_to_pn.c case.
- **~1 file with "unhandled node type N"** pops up after each round of
  fixes (different N each time) — the default case in `pvip_to_pn()`'s
  switch still returns raw `PN_NIL`, which is itself latently crash-prone
  the same way `PVIP_NODE_USE` was (fix 1). Consider hardening the
  `default:` case itself to return a safe no-op `EXPR(SRC(VALUE,
  PN_NIL))` instead of bare `PN_NIL`, as a blanket defense — would turn
  future "forgot to implement node type X" bugs into silent no-ops
  instead of crashes. Tradeoff: might mask bugs that are better left
  loud. Not done; flagged for a decision, not just "do it".
- **Several files with 2-second TIMEOUTs** (`while`/`until`/regex-heavy
  files) — likely infinite loops from mistranslated loop conditions or
  runtime stubs that don't actually terminate iteration. Not triaged.
- **`PVIP_NODE_PERL`** node type is entirely unhandled (falls to
  `default:`, returns bare `PN_NIL` — same crash risk as fix 1/4, just
  not yet hit by a sampled file). `perl:5<...>`-style embedding, rare.

## Process notes for whoever continues this

- Commit granularly, one root-cause per commit, with the bisection
  repro and the *why* (not just the diff) in the message — this file's
  "Fixed this session" list is reconstructed from exactly that, and it's
  what let later fixes (e.g. commit 7) quickly recognize "oh, this is the
  same union-aliasing pattern as commit 6" instead of re-debugging from
  scratch.
- Always re-verify the full set of previously-fixed minimal repros (keep
  them around in `/tmp/`, they're one-liners) plus `make test.p6 test.p2`
  before committing — this codebase has no CI, regressions are silent
  otherwise.
- `commit-priv` skill applies to this repo (`github.com/perl11/p2`, no
  `/SpexAI/` in the remote): `--author "Reini Urban
  <reini.urban@gmail.com>"` and `--date` outside 08:00–17:00.
- **TODO**: wire up a real `prove`-style pass/fail counter (not just
  exit-code) for `test/roast6/*.t`, e.g. a `test.roast6` Makefile target
  piping through `prove -e './bin/p2 -6'` or equivalent, so progress can
  be measured by assertions-passed, not just "didn't crash". The
  process-exit-code metric this session used systematically undercounts
  remaining bugs (silent wrong-output files look identical to genuinely-
  passing ones).
