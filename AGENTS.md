# AGENTS.md — p6/pvip and p5/P2-mode work notes

Guidance for continuing work on this repo's two spec-test corpora:
`test/roast6/` (915-file Raku/p6 spec tests, see the "p6/roast6" section)
and `test/roast5/` (482-file plain-Perl-5 spec tests, native p5/P2 mode, see
the "p5/roast5" section below it). Tracks what's fixed, what's known
broken, and how to keep triaging efficiently.

`prove` now works (`TAP::Harness v3.52`, confirmed via `prove --version`)
-- use it for real pass/fail counts instead of the exit-code-only scanners
below where convenient, e.g. `prove -e './bin/p2' test/roast5/base/`. The
exit-code scanners are still useful for fast corpus-wide triage sweeps
(finding *which* files to bisect) since `prove` runs serially and is much
slower across 482/915 files; `xargs -P` parallelism matters at this scale.

## p6/roast6

### Quick orientation

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
to this metric — it undercounts remaining semantic bugs. There's no `prove`
harness wired up yet for real pass/fail counting (see TODO below).

### Fixed this session (commits on `pvip` branch, chronological)

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

### Known-broken, not yet fixed (in rough priority order)

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

### Process notes for whoever continues this

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
- **TODO**: wire up a real `test.roast6`/`test.roast5` Makefile target using
  `prove` (now works, see top of file) for real pass/fail counts, not just
  exit-code. The process-exit-code metric used in both sections of this
  file systematically undercounts remaining bugs (a file that parses, runs,
  and prints wrong `not ok` TAP output looks identical to a crash in that
  metric) -- the TAP-aware python scanner in the p5/roast5 section below is
  a stopgap, not a replacement for real `prove` integration.

## p5/roast5

### Quick orientation

- No `-6`/mode-switch involved: `test/roast5/` is plain Perl 5
  (`#!./perl` shebang), runs through the native `syn/syntax-p5.y` grammar
  and `core/compile.c`, no `pvip`/p6 machinery at all.
- Same build/regression commands as the p6/roast6 section above
  (`make -j8 bin/p2`, needs `make syn/syntax-p5.c` first if `syntax-p5.y`
  itself changed; gate on `make test.p6 && make test.p2`).
- TAP-aware scanner (better than roast6's exit-code-only sweep -- counts
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
roast6 bisector above -- just run it directly instead of the brace-depth
bisector (roast5 files are much shorter and p5 error messages, while still
imprecise about *location*, at least correctly distinguish "didn't parse"
(`Syntax error near token 'X' before text "Y"` / `Internal parser error:
Couldn't parse all statements before text "Y"`) from real runtime bugs).

### Fixed this session

1. **`a310656`** — **the big one**: `front/p2.c`'s `p2_cmd_compile` forced
   `MODE_P6` for *any* `.t` file (added earlier for roast6, which also
   uses `.t`), silently routing all 482 roast5 files through the p6/pvip
   parser regardless of content. Removed the `.t` case (kept `.p6`/
   `.raku`, which really are p6-specific). This alone unblocked roast5
   from "~everything hits `p6 parse error`" to "runs through the real p5
   grammar". Uncovered and also fixed, in the same commit, a second bug:
   the `use v6;`/`use p6;` *bare* whole-file-capture form (added in an
   earlier session for roast6, 1f27e54) turned out to never actually
   work -- traced via debug prints that `G->pos` was unreliable inside
   that grammar action (greg's `end-of-file` subrule is a non-functional
   assertion in this grammar; the real "fully consumed" check lives in
   the top-level `perl5` rule's own action, not in any reusable subrule),
   so `use v6;` silently captured nothing and the rest of the file was
   parsed as *more p5 statements* -- "worked" by coincidence when that
   was also valid p5, broke confusingly otherwise. Removed the two
   broken grammar alternatives (kept the `use p6 { ... }` block form,
   which uses reliable balanced-brace capture) and moved bare-form
   detection to a cheap text pre-scan in `p2_cmd_exec`
   (`pn_buf_is_bare_use_p6()`), run once before any parsing is attempted.
2. **`2c46b62`** — `listexprs`/`callexprs` (list-literal and function-call
   argument lists) only accepted `,` as the item separator, not `=>`
   (fatcomma) -- despite the grammar already defining a `fatcomma` token
   and using it correctly for hash-literal items. Named-argument calls
   (`plan(tests => 2)`, found in **288/482 (60%)** of roast5 files as
   part of the standard `BEGIN { chdir 't' if -d 't'; ...; plan(tests =>
   N); }` boilerplate) were unparseable. Fixed by accepting
   `(comma|fatcomma)` as the separator in both rules.
3. **`e7f361f`** — `push @arr, LIST` (parenless multi-arg calls, the
   `BEGIN { chdir 't' if -d 't'; push @INC, '../lib'; }` companion to
   the fatcomma boilerplate) crashed/parse-failed. Two-part fix: negative
   lookahead on the single-arg parenless-call alternative so multi-arg
   inputs fall through to the list alternative, and replacement of that
   list alternative's corrupting `PN_SHIFT`/`PN_PUSH` action with the
   clean calllist-style shape. Full debugging saga (including the two
   prior failed attempts) preserved in the known-broken section below.
4. **`4c9eb15`** — `qw(word list)` literals entirely unsupported.
   Added a `p5_qw_words()` prologue helper (splits captured content on
   whitespace into LIST of VALUE(string) nodes; uses potion_source
   directly since PN_AST needs the complete GREG struct, unavailable in
   the prologue), a `qw` rule covering `()`/`[]`/`{}`/`//` delimiters
   (no `<>`: collides with greg capture syntax), `!utfw` so `qwx(...)`
   stays a normal call, and -- critically -- wiring into `expr` BEFORE
   `calllist`, which otherwise steals `qw(...)` as a call to a sub
   named qw. Two abandoned fix attempts documented in known-broken:
   assigndecl reorder (greg backtracker corruption, reverted), adding
   my/our/local to `keyword` (regressed `my sub` in upvals.pl since
   lexsubrout is commented out, reverted).

Verified both: `test/roast5/base/if.t` now correctly prints `ok 1`/`ok 2`
(previously test 2 silently never ran -- the file was being fed to pvip
and partially misparsed). `base/lex.t` now gets 44 lines further before
hitting a real, separate p5-grammar gap (heredocs) instead of erroring
immediately on `use v6`-style misrouting. `test.p6` ok/ok, `test.p2` same
4 pre-existing failures as clean tree.

### Known-broken, not yet fixed

- **`my @arr = <single-quoted string>` and `my @arr = qw(words with
  spaces)` still fail to parse** (everything else about qw and array
  decl works: scalars, double-quoted strings, numbers, barewords,
  paren-lists, all qw delimiter forms in scalar/expr context). Traced
  extensively with -Dp: the statement loses to a bareword-call parse
  of 'my' (via `sets sep?` where sep is optional), and with that path
  blocked (my/our/local added to `keyword`) assigndecl still genuinely
  fails, pointing at greg's backtracker/memoization corrupting state
  across the failed `assigndecl IF` stmt alternative rather than at
  grammar coverage -- the identical grammar paths succeed for `$`-sigil
  LHS. Two abandoned fixes documented in the fixed-list entry for
  4c9eb15: the assigndecl reorder broke `my @f = <anything>` wholesale;
  the keyword change regressed `my sub cl4 {}` in test.p2 (lexsubrout
  is commented out, so `my sub` currently parses via the call path that
  the keyword change blocked). Whoever picks this up: the `my sub`
  dependency means any keyword-type fix must first implement a real
  `MY SUB` grammar alternative.
- **`push @arr, LIST` FIXED (e7f361f).** The full saga: two overlapping
  parenless-call alternatives in `expr` (`c:call e:expr` vs
  `c:call l:listexprs`), PEG ordered-choice always picking the
  single-arg form and truncating multi-arg calls, two prior failed fix
  attempts documented here in detail (reorder → new segfault; lookahead
  alone → exposed the never-before-reachable `PN_SHIFT`/`PN_PUSH` bug in
  the listexprs action). Final working fix combined both lessons: the
  lookahead on `c:call e:expr !(- (comma|fatcomma))` (so multi-arg inputs
  fall through), PLUS replacing the listexprs action with the clean
  calllist-style shape (`set MSG's arg slot to LIST(l)`, return `c`
  unchanged) instead of the corrupting PN_SHIFT/PN_PUSH. Verified
  working: `push @a, 1;`, the 288-file `BEGIN{chdir 't' if -d 't'; push
  @INC, '../lib';}` boilerplate, single-arg `chr 101` still correct.
- Beyond that, the remaining ~460 failing files are architecturally the
  same situation as roast6's 493-file parse-error bucket: a long tail of
  individual p5-grammar gaps (heredocs confirmed in `base/lex.t`; others
  not yet sampled). Use the TAP-scanner + stderr-bucketing approach above
  to find the next highest-frequency one rather than guessing.
