# AGENTS.md — p6/pvip and p5/P2-mode work notes

Guidance for continuing work on this repo's two spec-test corpora:
`test/roast6/` (915-file Raku/p6 spec tests, see the "p6/roast6" section)
and `test/roast5/` (482-file plain-Perl-5 spec tests, native p5/P2 mode, see
the "p5/roast5" section below it). TODO items only — for what's already
been fixed, read `git log`, not this file.

`python3 test/sweep5.py` runs every roast5 file and buckets the failures (pass count, top messages).
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
# -6 is required: without it the files are parsed as perl 5 and every "parse error" is bogus.
# 20s: the 2000-8000 statement generated files (S15-*) take seconds; load makes 2s timeouts lie.
find test/roast6 -name '*.t' | xargs -P 16 -I{} sh -c '
  out=$(timeout 20 ./bin/p2 -6 "{}" 2>&1 >/dev/null </dev/null); rc=$?
  if [ $rc -eq 124 ]; then echo "TIMEOUT :: {}";
  elif [ $rc -ne 0 ]; then echo "rc=$rc $(echo "$out" | head -1 | cut -c1-110) :: {}"; fi
' > /tmp/roast6_errs.txt
wc -l /tmp/roast6_errs.txt   # nonzero-exit count
grep -c '^rc=139' /tmp/roast6_errs.txt   # segfaults: translator crashes, fix these first
```

**Caveat**: this only counts process exit code. A file that parses and runs
but prints wrong values (`not ok N` in TAP) still exits 0 and is invisible
to this metric — it undercounts remaining semantic bugs. Prefer `prove`
(see top of file) for a real count when feasible.

### TODO (in rough priority order)

- **p6 regex/control (done subset):** `s/pat/repl/[g]`, `s:g/../../` on a variable (`$x ~~ s///`) or `$_`
  (`p6_subst`, PCRE patterns and literal replacement, no Raku regex syntax, no `$0`/`$<n>` interpolation),
  `[op] LIST` reduce, `try`/`$!`, `X`/`Z`, `given`/`when`/`default` (desugared to `for`/`if`+`next`),
  `repeat {} while/until`. Open: `.subst`/`.match` methods, `$/` after a match, `$*PROGRAM-NAME`.
  `test/p6/given-when.p6` and `when-for.p6` are separate files because one combined file failed only
  in the Windows CI run (not reproducible, cause unknown).
- **Class gaps** (`class`/`has`/`method`/`Foo.new(k => v)`/`$.x`, multiple
  inheritance `class C is A is B` and roles `role R {..}` / `class C does R` work,
  see `PVIP_NODE_CLASS`/`PVIP_NODE_ROLE` in `syn/pvip_to_pn.c`). The translator
  keeps a registry (`p6_class_t`: attributes, `is` parents, `does` roles), computes
  the C3 order with `potion_c3_linearize` (`core/objmodel.c`, shared with p5's
  `use mro 'c3'` in `lib/p5/p5oo.c`) and flattens the constant `has` attributes
  of the ancestors (base first) and roles into the ctor signature. The Potion
  class chain is single (first `is` class), so `p6_compose` (`lib/p6/libp6.c`)
  copies the methods of the other C3 classes and of the roles into the class
  at runtime: own method > role method > first class in C3 order. Limits: the
  parents and roles must be translated earlier in the same process (a parent's
  non-constant `has` default is then not inherited: it is undef), no parametric roles
  (`role R[::T]`), no role conflict errors (the first role wins), no `does`
  type checks (`$x ~~ R`). `callsame`/`nextsame`/`callwith(..)`/`nextwith`/`self.SUPER::m(..)` and
  `obj.Class::m(..)` work inside class methods (`p6_callnext`/`p6_calldirect` in `lib/p6/libp6.c`: the
  translator knows the class and method name, `p6_mro` registers each class's C3 order at runtime;
  the next method is the next class's *own* table entry, so methods copied by `p6_compose` can run
  twice; bare `SUPER::m(..)`, `callsame` inside nested closures of a method and `samewith` are not
  done). `has $.x` also
  defines the accessor `x`, `self` and `$!x` work, `submethod BUILD(:$y)` runs at
  the end of the ctor (own BUILD only: no BUILD chain along the MRO, and the
  attributes are still auto-initialized from the ctor args first). A non-constant
  `has` default is computed unless `new` passes the attribute (ctor param defaults to
  the `PN_P5NOARG` sentinel, `p6_noarg_init`). Still open: `BUILDALL`/`TWEAK`,
  `$!x` writes from outside methods,
  `Foo.new(k => v)` named args only map positionally when the class was
  translated earlier in the same process, otherwise the VM `NAMED` op is used,
  which leaves attributes before the first named one unset (the VM default fill
  in `core/vm.c` CALL starts at `numargs`, not at the first unsupplied slot).
  Method sends now compile to real messages, which exposes crashes in natives
  that expect p5-style padded args (e.g. `$s.rindex(..)` -> `p5_rindex`,
  S32-str/rindex.t).
- **The large "parse error" bucket has no single common cause** — it's a
  long tail of individual advanced constructs. Confirmed NOT "parenless
  Test.pm calls" (those work fine standalone). Known constructs seen
  during sampling that don't parse:
  - `grammar NAME { token|rule|regex RULE { ... } }` now parses and compiles (see
    `p6_grammar` in `lib/p6/libp6.c`, `PVIP_NODE_GRAMMAR` in `syn/pvip_to_pn.c`): each
    declaration is translated to a greg `.y`, written to `.p2-syntax/NAME-<hash>.{y,c,so}` by
    `potion_syntax_compile` (`core/syntaxobj.c`, uses `bin/greg` + `POTION_CC`, cached by source
    hash) and bound to `NAME` as a `Syntax` object; `NAME.parse(str)` is a full-string match
    returning false or the match object (a table: `$m<rule>` is the submatch, a list when the rule
    matched several times, `~$m` the matched text, `"$text"` / `"$name"` its keys; every terminal is a
    leaf via `gm_leaf`, every rule `gm_begin`/`gm_end` in the generated header of `p6_grammar`;
    `<.rule>` still captures; `%h<a>`, `%h{'a'}`, `%h<a> = v` and `{k => v}` work in p6 mode: a `my %h = (..)` is a list of pairs, `{..}` a table).
    Subset only: quoted literals, `<rule>`, `<[a..z]>` classes, `[ ]`
    groups, `| *  + ? .`, `\d \w \s \h`; `rule` inserts `ws` where the source has whitespace.
    Done on top of that: `<.rule>` (text counts, no submatch), `<?before X>` / `<!before X>`,
    `Grammar.subparse(str)` (prefix match, rule `substart`), `parse($s, :actions(Obj))` (after the
    parse `core/syntaxobj.c` calls `Obj.RULE($/)` for every rule node, children first; `make X` stores
    `"$made"`, `.made` reads it), `$/` and `$<name>` (the global `$*MATCH`: the match being handled / the
    last parse), `+$m` numifies by the matched text. A `:` ratchet marker is ignored and `<?[..]>` / `<![..]>` are
    class lookaheads, and the `~` goal operator (`p6_goal` rewrites `A ~ B C` to `A C B`, so a missing goal is a plain
    mismatch, not a goal-failure exception). Missing: other lookaheads, rule bodies containing `}`, `*.made` whatever-star code,
    `proto token op {*}` with `token op:sym<+> {..}` alternatives (rule `op_sym_N`, node named `op`) and
    `slurp`/`spurt`/`unlink`, `.IO`, `.chars`/`.lines`/`.words` on strings exist in p6 mode (`lib/p6/libp6.c`,
    `test/p6/io.p6`). `grammar B is A` works for a parent translated earlier in the same process (its rules that
    B does not define are prepended, `p6_grammars` in `syn/pvip_to_pn.c`). p5 code reaches the objects with `Syntax->p5`,
    `Syntax->get(NAME)` (`Class->method` on a string that names a potion global class, `p5_meth`; the
    `potion` syntax is not part of bin/p2) and can match their `parse` ast (`test/p5/syntax-obj.p2`).
  - p6 `multi sub` / `multi method` / `proto` / `only` and `where` clauses work (`p6_multi_proto` in `syn/pvip_to_pn.c`): the
    candidates of a name are merged into one sub with optional parameters (`$__m0..`, default the p5 missing
    sentinel, so `P->p5args`) whose body is an if/elsif chain in specificity order (type 2, where 1 per
    parameter, ties by declaration). Compile time work: a `where * > 5` WhateverCode is inlined with the star
    replaced by the argument, not a closure (`p6_where_cond`), and a call of a multi sub whose arguments are all
    literals is bound at once to the candidate sub `NAME:cN` when arity, types and foldable where clauses
    (`* OP int`) decide it (`p6_multi_static`). A where clause of a plain sub or method is a check at the top of
    its body. `{ $_ * 2 }` blocks take the topic, `* + 1`, `*.chars > 2` chains are one WhateverCode closure.
    `where` also takes ranges, regexes (smartmatch) and `X ~~ Type`. Named parameters `:$x` (also in multi
    subs/methods, `p6_nget`/`p6_nstrip`) are optional slots after the positionals: a named argument arrives as a
    pair `(key, value)` in whatever slot it lands in, the prologue collects the pairs of all slots into the hidden
    `$__nt` and binds them by name. `:$x!` is required (a multi candidate needs it given, a plain sub dies); `*%rest` collects the pairs no named parameter took (`p6_named` mode 2, plain subs only, not multi); a pair passed positionally is
    taken for a named argument, named-ness is no part of the multi candidate ordering.
    A `proto sub` body wraps the dispatch (`{*}` is the chain, `p6_star`; calls are then not bound statically);
    only `proto sub`, not `proto method`, and the proto's own signature is ignored.
  - p6 macros work: `macro NAME($a, ..) { quasi { ... {{{ $a }}} ... } }` is run when a call `NAME(args)` is
    translated (`PVIP_NODE_MACRO/QUASI/UNQUOTE` in `syn/pvip_to_pn.c`, same design as the p5 macros: the
    arguments are asts, every expansion splices into a copy of the quasi block). `quasi` and `macro` are
    reserved words in `syn/syntax-p6.y`. Open: macros as method calls or without parentheses,
    hygiene, `:COMPILING`. Test: `test/p6/macro.p6` (checked against raku).
  - `sub infix:<op>` / `prefix:<op>` / `postfix:<op>` definitions work (word or symbol operators; the parser learns them from a
    text scan, `p6_uops_scan`, and matches them character by character, `p6_uop_mark/step/done`, so that the rules consume
    input and the buffer is refilled: a predicate that advances `G->pos` by itself made `power` "possibly left recursive"
    in the p5 grammar and could not see past the buffered input in the p6 one). A user infix has the precedence of `+`.
    `infix:<;>` is excluded from the scan (Rakudo issue #2408: the statement separator wins; learning `;`
    made the sub's own body `$marker = 1; 0, 0` parse `;` as the operator and recurse until a stack
    overflow, S06-operator-overloading/semicolon.t). semicolon.t: 2/3 ok; the remaining `not ok 1`
    is `+@a` (numeric coercion of a list — Raku's `+(1,2,3)` gives the first element; p2 has no
    `plus` for tuples). `@a = EXPR` wraps a non-list RHS in a one-element list (the LIST_ASSIGNMENT
    fallback in syn/pvip_to_pn.c sends the RHS through `p5aslist` when the LHS has an `@` sigil:
    `my @a = 1` / `@a = (1)` / `@a = "x"` → 1 element, checked against raku; test/p6/list.p6).
  - p6 `grep` / `map` list methods with a block **or a non-Code matcher**, and the function forms
    `grep(MATCHER, LIST...)` / `map(MATCHER, LIST...)` (`p6_list_grep`/`p6_list_map` + the lobby natives
    `p6_grep_fn`/`p6_map_fn` in `lib/p6/libp6.c`): the matcher is resolved once per call by
    `p6_match_setup` — a closure is called with each element (it is the topic `$_`), a type (a bare `Int`
    is the class's vtable object; or its name string) is an `p6_isa` test per element, any other value a
    conservative `p6_smartmatch`; calling a non-Code matcher used to crash (`potion_call` on a vtable,
    S32-list/grep-{v,p,kv}.t segfaulted). The function forms are varargs like Raku: `p6_wrap_matcher_args`
    (syn/pvip_to_pn.c) wraps the trailing items in a tuple for the (matcher, list) natives, decodes a
    trailing `:k`/`:kv`/`:p` (a pair whose value is the bare true/false node only the colon rules
    produce; `:v`/other → 0) into `*flag_out`, and passes a single remaining item as the list as is (a
    bare array stays a nested element when mixed with other items, as in Raku); a bare scalar list
    (`grep(Int, 2)`) is a one-element list. The `:k`/`:kv`/`:p`/`:v` flags work in both forms
    (`p6_grep_flag` decodes the int or the `(key, true)` pair; `:k`→indices, `:kv`→flat index,value,
    `:p`→(index=>value) pairs, `:v`/none→values): the flag function form is emitted as the list method
    send `@l.grep(M, :k)` (the proven `EXPR(TUP2(recv, MSG))` shape) with the flag a `p6_pair` CALL node,
    not a hand-rolled (matcher, list, flag) lobby tuple (that clobbered an adjacent GC tuple: deterministic
    compile-time segfault) nor a bare runtime tuple in the AST arg list (that mis-binds the named flag).
    Open: `:p` pair *stringification* (p2 prints `(0, 1)`, raku `0 1`), mutating `$_` in the block
    (it is a copy, `@array.grep({ $_ ~= "c"; 1 })` does not write back), junction matchers
    (`@a.grep(any(@b))`), the method form on a non-tuple (`42.grep({ 1 })`), the adverbial-block syntax
    `grep():{...}` (parse error; blocks S32-list/grep-k.t, `#?rakudo skip` there too), and `throws-like`
    Bool matchers. Roast (plan 17 each): S32-list/grep-v.t 9 ok, grep-kv.t 9 ok, grep-p.t 8 ok
    (the rest hit the gaps above, e.g. `:p` pair stringification and the non-Code matchers).
  - sigilless params `sub f(\x) { x }`
  - multiple dispatch signatures `multi sub f(A $x) | (B $x) {...}`
  - `$?LINE`, `$?MODULE`, other compile-time "magic" variables (some are
    stubbed in libp6, not all)
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
- **Timeouts under a 2-3 second limit**: most were not loops. The 500-8000 statement generated files
  (S15-normalization/nf*, S15-nfg/mass-*, emoji-test) took seconds because the JIT assembly buffer
  grew linearly (fixed: `PN_FLEX_NEEDS` doubles) and one crashed in a 10 argument call (fixed:
  `potion_jit_proto` reserves the widest call). Those files still take 1-3 s to parse+compile and fail
  at run time (`Uni`, `.NFC`, `.chars` are not implemented). Use `timeout 20` when sweeping and rerun
  the TIMEOUT list serially before assuming a hang; none of the 48 files hangs any more.
- **`CATCH { default {..} }`** (`p6_catch_wrap` in `syn/pvip_to_pn.c`, `p6_catch` in `lib/p6/libp6.c`): the other statements of
  the block run as a closure under an eval frame, the handler (a once-loop, so `when`/`default` `next` works) gets `$!`/`$_`.
  Open: the exception is always swallowed (no rethrow when no `when` matches), `return` in the body, `.message` / typed `when X`.
- **`PVIP_NODE_PERL`** node type is entirely unhandled (falls to
  `default:`, returns bare `PN_NIL`). `perl:5<...>`-style embedding, rare.
- `make test.roast6` runs `OK_ROAST6` (in test/roast6.sh) with `prove` via `test/roast6.sh`; `python3 test/newok6.py [--write]` lists/adds the
  roast6 files that really pass (plan met, no failure). The Test functions (plan ok is isnt is-deeply like cmp-ok skip todo lives-ok
  dies-ok ...) are natives in `lib/p6/p6test.c` (`use Test` itself is a no-op). 20 of 915 files pass.

## p5/roast5

### Orientation

- No `-6`/mode-switch involved: `test/roast5/` is plain Perl 5
  (`#!./perl` shebang), runs through the native `syn/syntax-p5.y` grammar
  and `core/compile.c`, no `pvip`/p6 machinery at all.
- Same build/regression commands as the p6/roast6 section above
  (`make -j8 bin/p2`, which regenerates `syntax-p5.c` from `syntax-p5.y`;
  gate on `make test.p6 && make test.p2 && make test.p5`).
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

- **`p2 -d` loop variables are done by the p5 parser** (`$P2::repl` set by the loop, `p5_vn` and `p5_repl` in `syn/syntax-p5.y`;
  `front/p2.c` only reads lines: `repl_open` decides when an input continues; `doc/debugger.md`; `test/repl.sh` = `make
  test.repl`, part of `make test` and so of CI, it skips itself where `script` is not util-linux, i.e. on the BSDs and macOS).
  A parse of `<eval>` (and the nested `<@{[]}>`/`<s///>` ones) with the flag set maps every `$x`/`@a`/`%h` that the input does
  not declare with `my` to the lobby global `$REPL::x`; `core/compile.c` stores such names with `OP_GLOBAL` (an element store
  `$REPL::a[i] = v` reads the global through `OP_SELF`/`OP_LOADK`/`OP_BIND`/`OP_CALL` and then goes the callset way, `sess_elem`),
  arrays and hashes are created at the first mention (`p5_see`, no program-start vivification), `$x++` is assign and adjust
  (`p5_sess_var`), the input is one `local` scope (`repl-mark`, `p5_local_depth > 1` means a block), `for my` is `MYFOR`
  (always lexical), `p5defsub` also defines a `main::` sub as a lobby global (so later inputs call it), the sub names persist in
  `p5_subnames`. Heredocs: `p2_parse` expands them itself, `repl_open` returns 2 until the terminator. Open: nested
  auto-vivification, `<<~` (not in the p5 grammar), a sub named like an interpreter method, the `potion` loop (one program per line).

- **tie (lib/p5/p5tie.c):** `tie VAR, CLASS, LIST` (also `tie(my $x, ..)`, `tie my @a`, `tie my %h`), `tied VAR`,
  `untie VAR`. The variable holds a Tie object (TieScalar/TieArray/TieHash, one ivar: the object TIE*
  returned) that forwards to FETCH/STORE/FETCHSIZE/PUSH/POP/SHIFT/SPLICE/DELETE/EXISTS/CLEAR/FIRSTKEY/NEXTKEY/..
  The parser remembers the tied names (`p5_tied_name`): a scalar read becomes `p5tfetch($x)`, `$x = v`
  `p5tstore`, `$a[i]`/`$h{k}` an `at` message (`p5_tel`) and the store a `put` (`p5_fix_assign`), `@a = LIST`
  `p5tassign` (`p5_tie_aggr`), foreach over `@a` iterates `p5tlist(@a)` (all FETCHes happen before the loop
  body, perl interleaves them). Limits: names are tied lexically-after-the-tie in source order only (a sub
  defined before the `tie` still reads the plain variable), `%h` / `@a` read as a whole list
  (`my @l = %h`, `print %h`) are not forwarded, no tied handles (TIEHANDLE), no `local` on tied elements,
  `$h{k} = v` as an expression yields the container. Test: `test/p5/tie.t` (+ .expected, cross-checked with perl).
- **perl 5.38+ `class` feature (`use v5.42; use feature 'class'`) is p2's object syntax**:
  `class NAME [VERSION] [:isa(PARENT)] {..}` / `class NAME;`, `field $x|@a|%h [:param[(name)]]
  [:reader[(name)]] [= | //= | ||= EXPR]`, `method name [(sig)] {..}`, `ADJUST {..}`, `$self`,
  `NAME->new(k => v)`, chained `a->b->c` sends, `ref($obj)`, `$obj isa NAME`. Rules `classdecl`..
  `fielddecl` and the `p5_class_*` C helpers in `syn/syntax-p5.y`, runtime in `lib/p5/p5class.c`.
  A class is a Potion class (`NAME = class (params) {field inits, ADJUST}`, methods `NAME m =
  proto`); greg actions run after the whole parse in order, so the class is assembled by
  `p5_class_end` from the item tuples and the C registry `p5_classes` (param names, filled by the
  actions in order). A scalar field is the ivar path `/x` (`p5_subst_fields` rewrites `$x` in
  method/ADJUST/initializer ASTs, `$x++` becomes `$x = $x + 1` because AST_INC only knows
  locals); `@a`/`%h` fields are aliased to locals on method entry, so assigning a whole new
  list inside a method does not write back. `:param`s are ctor params `$__p_NAME` defaulting to
  `PN_P5NOARG`; `NAME->new(..)` with literal keys is rewritten at parse time to a positional
  call, else `p5new` (runtime). A subclass flattens the parent's params and ctor body
  (`p5_cls_store`) and gets its own ivar names `Class::x`; method defaults (`$m = 0`) are
  `PN_P5NOARG` + body prologue, because message-send call sites cannot fill them (JIT). Ivar
  names must be interned strings (`PN_STRN`), `potion_str_format` gives an unusable bytes object.
  Missing: `SUPER::`, `__CLASS__`, anonymous/lexical (`my method`) methods, `:reader` with
  array results beyond the plain tuple, `DOES`, `class` before its use in another file, a
  dynamic invocant (`$class->new`), field references inside closures within a method, a `my $x`
  that shadows a field of the same name. `test/p5/class*.p2` are cross-checked by
  `test/runtests.sh -p2` against perl >= 5.42 (`PERL=~/perl5/perlbrew/perls/perl-5.44.0/bin/perl`).

- **cperl macros (`t/comp/macro.t`, `pod/perlcperl.pod` "Macros"): declarations and plain call
  expansion done** (`p5_macro_*`, `p5_quasi*` in `syn/syntax-p5.y`). `macro NAME|'op' [:attr] [(sig)] {..}`
  is compiled and run by its greg action at parse time (actions run in source order, so before the
  actions of later calls; no prescan needed), the closure lives in the rooted `p5_macro_tab`, the
  declaration leaves no code. `NAME;` / `NAME(args)` call sites are replaced IN PLACE (`p5_become`)
  by the returned ast; args are passed as their ast. `quasi {..}` is the tuple (block-ast,
  unquote values), a backtick `` `stmts` `` is a hole (`p5unquote` MSG); every expansion splices the
  values into a fresh copy of the block (`p5_ast_copy`), so a quasi can be expanded any number of
  times. `NAME args` without parens works. Infix macros for `+ - * / . == < > <= >=` work (`p5_opmacro`: the macro `ATTR:OP` gets the asts of the left and right
  operand in source order). User operators work too: `macro '!!' :prefix`, `:postfix`, and `:infix` with a symbol or a word (`macro comet :infix`);
  the parser learns them from a text scan of the source (`p5_prescan_macros`, `p5_mop`, `minfix`/`mprefix`/`mpostfix`)
  because a macro is only defined by an action after the parse. Open: `:circumfix/:postcircumfix` (the delimiter
  pair syntax is not specified anywhere I could find), `COMPILING::`, hygiene
  (a quasi sees the caller's lexicals, not the macro's), macro lexicals from outside the
  macro, `{;}()` nil blocks, `throws_like`. Tests: `test/p5/macro-decl.p2`, `macro-expand.p2`.
- **`%^H` / `$^H` are block scoped at compile time** (`p5hhpush`/`p5hhpop`, `lib/p5/p5number.c`): the parser's action phase
  runs in source order, so `p5_local_blockstart`/`p5_local_blockend` (blocks) and `p5_viv_begin`/`p5_viv_prologue` (files, which
  start with clear hints, as `require` does) save and restore the lobby global `%^H` (a table clone) and the hints int.
  `$^H{k}`, `exists`/`delete $^H{k}` and `%^H` go through the global via `p5_tied_name` (the `at`/`put` path), because an
  element assignment to an unknown name would create a local of the BEGIN's eval proto. `eval STRING` compiles with the hints of
  its site (`p5hhsnap` at parse time, `p5evalstrh`). Open: `tie %^H`, `%^H = ()` (assigns a local), `"$^H{k}"` in strings,
  `our`/file lexicals assigned inside BEGIN are not visible outside it (comp/hints.t 15-18, 21-23), `${^WARNING_BITS}`.
- **`%ENV` / `%Config` are built on first use** (`p5envinit`, `lib/p5/p5eval.c`; the parser calls it from `p5_vn` / the `%ENV` hashvar and
  starts a program that mentions them with it, so compiled programs have them too). Building them at every startup made the number
  of allocations depend on the size of the environment and moved the collections of unrelated programs: on the Windows CI that hit the
  open GC crash deterministically. `%ENV` is copied back into the process environment before `system`/backticks/`popen`
  (`p5_env_sync`). Gotcha seen twice: `const char *p = PN_STR_PTR(s)` followed by anything that allocates (`potion_define_global`, a
  `PN_STR`, a lazy init) is a use after free; re-read the pointer or finish the allocations first (p5_chomp, p5_vn).
- **opbasic/*.t status**: arith.t passes (in `OK_ROAST`). cmp.t passes (in `OK_ROAST`); concat.t fails on `use bytes`
  semantics; magic_phase.t on DESTROY (no refcounts; `${^GLOBAL_PHASE}` START/CHECK/INIT/RUN/END work, `p5_end_append`). `qq` takes `! / ( [ { ' | " ~ ^ < >`
  or any one non-ASCII (extended UTF-8, up to 7 bytes like perl) character as delimiter (`p5_qgd_open/at` predicates);

  Every `==` is `p5numeq(a, b)` (no identity shortcut: the string "NaN" equals itself in identity but not numerically); an assignment operand
  (`($x = f()) == 1`) still uses the identity test only, because an assignment as a call argument is lost.

- **p5 natives live in `lib/p5/` (libp5.so), not in core/**: `p5file.c`
  (open/close/eof/readline/print/say FH on top of PNFile; read buffers in a
  side table by fd), `p5table.c` (@_, a..b, map/grep/sort), `p5string.c`
  (ref, print STDERR, substr/index/rindex/join/sprintf), `p5eval.c`
  (eval/die/warn/do/caller), `p5number.c` (perl `/ % **` and %.15g number
  strings, `use integer` = `$^H |= 1` (HINT_INTEGER; `$^H` reads/writes map to `p5hints()`, block scoped via the
  p5 local machinery), `| & ^` as p5bor/p5band/p5bxor, + - * for double operands, overriding the core methods; the P2 VM/JIT always dispatch those
  ops to methods, Potion core keeps its inline integer paths). `front/p2.c`
  loads it at startup (`p5_load`, `Potion_Init_libp5`). New p5-only natives
  go there, never into core/. File handles: bareword `FH` is the lobby global
  `*FH`; lowercase barewords only count after an `open(name, ...)` seen by
  the parser (`p5_fh_note`), else `print lc "X"` would be a handle. File system builtins (`p5file.c`, end of the file): `mkdir rmdir chdir rename link symlink readlink truncate chmod chown utime stat lstat
  opendir readdir rewinddir closedir` (a directory handle is a PNFile with fd -1, the names in `path`, the position in `mode`; `readdir`
  in list context is `p5readdirall`, via `p5_listctx`); a failure sets `$!` to the text. `stat` returns the 13 element list, the empty list on
  failure (no scalar-context boolean). `(LIST)[i,j]` list slices work (`p5_is_call_item` spreads `(stat ..)[2]`). Not done: `fchmod`/`fchown`
  "unimplemented" errors, `*FH{IO}`, `stat _`, `open(my $fh,..)` inside the arguments of a call (the `my` is not visible after it).
  Missing:
  `eof` without args, `DATA` is a temp file bound by the first statement, `binmode`/`seek`,
  dup modes (`>&STDOUT`), `printf FH`, `local *FH`, `$!` beyond open errors.
  Gotcha: greg predicates (`&{ }`) see a valid `yytext` but not `yyleng`.

- **GC hazard: raw `PN_STR_PTR(x)` across an allocation.** The collector moves strings; a
  `char *` taken from a GC string before an allocating call (PN_STRN, PN_PUSH, potion_str)
  dangles afterwards. `potion_str`/`potion_str2`/`potion_strcat` copy a source inside the
  GC heap first, but a pointer kept in a local variable over several allocations must be
  re-read from the PN each time (p5_split did not: FreeBSD's allocator exposed it).

- **Regex (PCRE2, `core/regex.c`, p5 natives in `lib/p5/p5string.c`):** `m//`, `m{}` `m!!` `m##`
  etc., `qr`, `s///`, `tr///` / `y///` (flags c d s r) with any delimiter (`rx*` rules, generated
  per delimiter). `$&`, `` $` ``, `$'`, `$1`.., `@-`, `@+`, `%+` (`$+{n}`, `$-[n]`, `$+[n]`, also in
  strings) are published by each match (`potion_regex_publish`; `$N` reads the tuple global `@^M`).
  `m//g`: scalar context walks the matches (`p5matchg`, pos kept per variable name in a table, reset
  when the string content changes or the match fails: re-assigning the *same* text keeps the pos, a
  bare `$x =~ m//g;` statement is a plain match), list context (`p5_listctx`: list assignment,
  `() = `, call args) is `p5matchall`, a list match without /g is `p5matchl` (groups or `(1)`).
  `pos($s)`; `m//gc` keeps pos on failure; a statement `$x =~ m//g;` walks only when the source mentions `pos(`, else it is a plain match
  (`p5_pos_used`: the pos table cannot tell that `$x = 'same text'` was assigned again). `s///` stores the new string and its value is the count (`p5subcount`); a replacement
  with `$`, `@`, `\\` or `/e` is a closure run per match (`p5substf`, the text is parsed with
  `p2_parse`); `(my $t = $s) =~ s///` works. Still missing: `\U \L \u \l \Q \E` in strings and
  replacements, `/ee`, `/r` on `s///`, `\G`, `split` with captures, `$/`, `${^OPEN}`,
  `@{^CAPTURE}`, `(?{ })`; `s///` on `foreach` aliased elements (`s/x/y/ for @a` does not write back).
  Gotcha: growing the set of pre-created lobby globals (e.g. `$1`..`$32`) made `$2`, `$7`.. read
  undef in BOTH JIT and `-B`: pre-create the few match globals in `potion_regex_init`.

- **`@_` limits (design: see `p5_sub_proto` in `syn/syntax-p5.y`).** Plain
  subs whose body mentions `@_` get 12 hidden optional params `$__aN`
  (default sentinel `PN_P5NOARG`) and a prologue `@_ = p5args($__a0..$__a11)`
  (`p5_args` in `lib/p5/p5table.c`). More than 12 arguments only
  work through `p5callv` (spread calls, calls with > 12 items): the 12th hidden param then holds the
  tuple `(PN_P5REST, a12, ...)` that `p5args` unpacks; plain JIT call sites still drop them.
  `&f;` passes the current `@_`. `@_` aliasing (`$_[0] = 1` writing back) is not implemented;
  `sub f(@r)` still binds only one scalar. A plain array operand of `+ - * / % . == < > <=>` and
  the rhs of a scalar assignment is its length (`p5_cond`/`P5N`); `cmp`/`lt..` keep the tuple, a
  `return @a` has no call context (`wantarray` is missing), so `my $n = f()` gets the list. The x86-64 JIT pads every call site with the sentinel up to
  12 args when the program has such a sub (`P->p5args`, `potion_x86_call`);
  other JIT targets still run those programs in the bytecode VM, because the
  call site fills defaults from `protos[0`] only. Natives with more C
  parameters than the call site passes read UNINITIALIZED STACK for the rest
  (a 3-arg `join` call left 6 garbage C params in `p6c_f_join`, S32-str
  sprintf-*.t segfaulted): the x86-64 JIT now pads every call site with NIL
  up to the widest native (10 C slots, `X86_PAD_MAX`, `potion_x86_call`) and
  `potion_jit_proto` keeps the SysV frame protoargs at >= 6 (Win64 >= 12,
  arg 10 lives at 80(%rsp)) so those stack slots exist. The bytecode-VM path
  had the same hole: `potion_call` (native dispatch) now pads fixed-arity
  calls to the same 10 C slots with NIL, so `potion_closure_string`'s
  `maxlen` (and every wider native) no longer reads uninitialized stack on a
  `-B` run. This is a plausible candidate for the OPEN flaky-Windows-CI
  single-test failures (uninit-stack read signature: fails once, rerun
  passes). There is no call stack yet:
  `caller` needs one (closure + args per call); a bare `shift`/`pop` outside
  a sub is rewritten to `@ARGV` at parse time (`p5_top_args`).
  A leading `my $x = shift;` run and a following `my ($a, $b) = @_;` (plain scalars, in order, not
  the last statement, `@_` not used again) are recognised with the tree matcher of
  `core/compile.c` (`potion_ast_match` / `potion_ast_find`, patterns are PNSource trees with
  `potion_ast_wild` / `potion_ast_rest` wildcards, see `core/ast.h`) and become plain optional
  parameters without hidden `$__aN`: no @_ tuple, much fewer registers. Use the same helper for
  further tree optimizations; never mark nodes in the unused `a[1]`/`a[2]` slots, the gc only
  follows `potion_ast_sizes[part]` kids.
  The matcher is also a set of PNSource methods for user macros (`core/compile.c`: `match`,
  `find`, `pattern(code)` with `$__0`..`$__9` wildcards, `wild/some/rest/lead/alt`), see
  `test/p5/macro-match.p2`. The tree predicates build their rooted patterns before the parse
  (`p5_init_patterns`): a pattern built on first use allocates and a gc moves strings, which broke
  callers holding a raw `PN_STR_PTR`.
- **`test/p5/test.pl` is a small p2-subset TAP library** (plan, ok, is,
  isnt, pass, fail, skip, skip_all, diag, note, done_testing/done), not
  perl's real `t/test.pl`, which p2 cannot parse yet (about 50 of 88
  top-level chunks fail: sub prototypes `sub f ($$)`, `map`/`grep` blocks,
  `local`, `wantarray`, `//`, `@{[...]}`, `qx`). `require "file"` is
  expanded at *parse time* (`p5_require`), a BEGIN block containing one
  stays in the program. Known limits: no `like`/`is_deeply`/`cmp_ok`,
  `skip()` cannot leave its SKIP block, named subs may be called before their definition (`p5_predeclare`:
  when an earlier top-level statement mentions a later top-level sub, the
  assigned names are predeclared and the subs hoisted; not done inside
  `require`d files, hoisting there breaks compiled .tc programs, not
  root-caused; `sub` inside blocks is never hoisted), and
  `last`/`next` outside a lexical loop is a compile error. Run roast5 with
  `test/roast5.sh -e bin/p2 <dir-or-files>`; `make test.p2` runs `OK_ROAST` (list in test/roast5.sh, the default of the script).

- **Strings and blocks (p5):** `"\Q..\E"`, `\U`, `\L` wrap the pieces of a double quoted string in quotemeta/uc/lc (markers in
  `P->dqpieces`, `p5_dq_finish`); `\u`/`\l` (one character, also before `\L..\E`) work; `@{[ EXPR ]}` interpolates (text up to the first `]}`). `map`/`grep` blocks get `$_` as a closure parameter. `{ BODY } continue { CB }`
  on a bare block is a once-loop (`p5_bare_cont`); the continue block is inlined into the loop test, so a `return` in it leaves the sub. `sub NAME(&@)` is noted by the line pre-scan (`p5_blocksubs`) and `NAME BLOCK LIST` becomes
  `NAME(sub BLOCK, LIST)`: only subs defined with the prototype at the start of a line, no imported List::Util `first { }`,
  other prototypes stay ignored.
- **Typeglobs** (`lib/p5/p5glob.c`, `p5glob*`): `*name = sub {..} / \&f` defines the sub, `*a = *b` aliases every slot, `*a = \@x / \%h`
  sets the array/hash slot, `*a = \$x` copies the value, `*{"Pkg::$n"} = ..` computes the name, `*name{CODE|ARRAY|HASH|SCALAR|IO|NAME|PACKAGE}`
  reads a slot; a glob object (`p5glob`) stringifies as `*main::name`. The slots are the sub registry and the lobby globals
  `@name`/`%name`/`$name`/`*name`. Limits: a plain `*NAME` expression is still the file handle global, not a glob object;
  variables assigned at the top level of a file are locals of the main proto (not lobby globals), so a glob or a symbolic
  reference cannot see or alias them; arrays are rebound on assignment and `push` (tuples are not resized in place), so `\@a`
  and a glob slot are snapshots; `local *a = ..` does not restore.
- **foreach aliasing**: `for $x (@a)` / `$_ op= .. for @a` writes the loop variable back to `@a` (`p5_for_writeback`: at the start of the next
  iteration and after the loop, so `next`/`last` work). Only for a plain `@name` list: no `@$r`, slices, `keys`, several lists, hash
  values, `map`/`grep` `$_`, and the array is not aliased while the body runs (`$a[0]` still has the old value there). A global loop
  variable (no `my`) is saved before and restored after the loop.
- **GC stress**: build with `-DP2_GCSTRESS` (all objects, `touch` the sources: the header is not a make dependency) and run with
  `P2_GCSTRESS=N` (a collection at every Nth allocation after `potion_init`; during init the boot page is not collected, its
  objects are protected by `GC_PROTECT`). A copy of the tree runs the libs of the original tree (absolute RUNPATH): set
  `LD_LIBRARY_PATH=copy/lib`; get backtraces of ASLR dependent crashes from `coredumpctl gdb`. Bug classes found so far (all fixed):
  an old owner written with a young value and no `PN_TOUCH` *before the next allocation* (Proto fields, vtables, closure upvals,
  `PNFile.path`, method tables); a slot reserved but not yet filled while an allocation collects (`kh_put` in `potion_def_method`,
  new `P->vts` slots); a static AST pattern or registry not rooted right after it is built; a raw `PN_STR_PTR` kept across an
  allocation (`p5_meth_step`); and `potion_gc_realloc`: its new object has the size fields of the old one until the caller grows
  it, a collection there (the write barrier list filling up) copied it truncated and `kh_resize` wrote past the copy into the next
  object (NetBSD, mro/complex_c3.t, only for some environment sizes); and the x86 JIT's SETLOCAL/SETUPVAL stores into a
  closure box (`potion_f_touch`). The x86 JIT OP_PROTO also needs the barrier after EVERY upvalue box store (the next `potion_ref` allocation may promote the closure),
  not only at the end: that was the N=1 crash of local-list, num-perl-semantics, package-vars, require, state (and the flaky
  Windows-only CI failures of the same tests); raw `PN_STR_PTR(x)` arguments evaluated before a call that allocates (`p6c_join`,
  `p5_synfail`) crashed p6 arith/list/where and eval-string-syntax; and a GC mark that skipped `PNClosure.name`
  (a PNString reference, minor AND major): after a collection the old-region copy's `name` pointed into the freed
  nursery, and the first deref — the `potion_sig_check` "Required N, given M" error path's `AS_STR(cl)` →
  `potion_closure_string` reading `->name` — segfaulted; repro `./bin/p2 -6 -B test/roast6/S32-str/sprintf-c.t`
  (deterministic 20/20, now clean; gdb: `p/x` per frame, the backtrace's decimal param displays are stale spill
  artifacts). All p5 and p6 tests, and the roast5/roast6 OK lists, pass at N=1..20 (JIT; the `-B` runs were verified
  without the `-DP2_GCSTRESS` compile flag — the env var alone is a no-op). Run the p5 tests from the tree root too
  (filehandle.t uses a path relative to it). Run the p6 tests from the tree root (`libsyntax-p6` is found relative to it). `N>=1000` passes everything.
- **ASAN**: `make ASAN=1 CC=clang` (config.mak) in a copy of the tree, run with `ASAN_OPTIONS=detect_leaks=0:detect_stack_use_after_return=0`
  (the fake stacks hide the locals from the conservative GC) and `-B`: the x86 JIT crashes under ASAN. p5/p6 tests and the roast
  OK lists are clean; it found the `lib/aio.c` ivar tuple overflow (5 slots, 8 names) that corrupted the interned strings.
- **OPEN: flaky GC crash on Windows (mingw x64, bytecode VM).** `P2_GCSTRESS=N bin/p2 -B test/p5/tie.t` (N=3,4,6,8,9,12) or a
  three line `package Counter; sub TIESCALAR { my ($c,$v)=@_; my $x=$v; return bless \$x, $c }` segfaults in the compiler
  (`potion_source_asmb`, AST_PROTO whose sig LIST node has a zeroed child tuple); plain Linux stress passes for N=1..40, and the
  commit 38af919 (before tie) crashes the same way (N=13), so it is old: it shows up as random single-test failures of the
  Windows CI job (given-when, rule-in-class-Str, num-perl-semantics, require, roast5 op/exp.t), a rerun usually passes.
  Already fixed on the way: callee-saved registers spilled before the stack scan (`__builtin_unwind_init`), the object
  start maps (`P2_GCMAP`) are on for Windows (stale integers collide with the small heap addresses), closure upvalue barrier
  in the JIT. Debug with the CI temp-branch technique: a workflow that builds with `#define P2_GCSTRESS 1` prepended to
  core/potion.h and loops over N, plus `gdb -batch -ex run -ex bt`.
- **OPEN: deterministic -B GC crash in S32-str/sprintf-{d,o,s,u}.t (Linux).**
  `./bin/p2 -6 -B test/roast6/S32-str/sprintf-d.t` (and -o/-s/-u; rc=139 ~20/20;
  sprintf-c.t is fixed by the closure-`name` mark above) crashes inside the minor GC:
  `potion_mark_minor`'s PN_TTUPLE case dereferences a tuple element holding garbage
  (observed: a libp2 code address, a C-stack address, and `P` with clobbered mid bits,
  0x749FF7F38088 vs P=0x7FFFF7F38088) — a nursery object overlap or out-of-bounds write
  happened BEFORE the first GC; the GC faithfully copies it, then chokes. The corrupted
  tuple's header size field (alloc=6) and its `alloc` field (9) disagree (stale size on
  tuple grow, or an overlapping object). It is NOT the closure-`name` bug (that one is
  fixed: sprintf-c.t passes). The DEBUG build is ASLR-deterministic (same addresses every
  run) — convenient for watchpoints; the JIT compiles and maps exec pages even in `-B`
  mode; `P2_GCSTRESS` without the compile flag is a no-op.
- **Punctuation variables are lobby globals** (`$\ $, $/ $; $@ $! ...`: `$` + one punctuation char except `_`): an assignment
  compiles to `OP_GLOBAL` (`core/compile.c`), a read is the global lookup. `print`/`say` honour `$\` (`p5_lprint`, `p5_print`; `say`
  ignores it) and `$,` (only when the source text mentions `$,`: `p5_ofs_used`, `p5_cat` then joins with `p5joins`). `<FH>` honours
  `$/` (undef slurps, "" is paragraph mode, any string up to 63 bytes; list-context `<FH>` still splits at "\n"), interpolated
  arrays join with `$"` (when the source mentions it, `p5_listsep`). Defaults `$/` and `$"` are defined in `p5_file_init`/`p5_string_init`.
  Not read by anything yet: `$;` (multi-dimensional hash keys). `"$;"`/`"$,"` are not interpolated in strings.
- **In-place edit, `-n`/`-p`/`-i[ext]`, `select`, `system`/`readpipe`**: `$^I` (a lobby global; compile.c treats `$^X`-style caret variables
  except `$^H` as globals on assignment) makes `<>` (`p5_argv_read`, `lib/p5/p5file.c`) move each file to its backup
  (`bak.*` prefix, `*` = basename, no `*` = suffix, `*` alone = no backup), recreate it as `ARGVOUT` and `select` it.
  `select(FH)` sets the handle bare `print`/`say` write to (`p5_sel_out`); `select(undef,undef,undef,SECS)` sleeps.
  `system(CMD)` / `readpipe(CMD)` (one string, via `sh -c`) set `$?`; backticks and `qx` are not parsed yet. `unlink LIST` (up
  to 6 names), `$$`. `front/p2.c` takes perl style switch bundles: `-n`, `-p` (wrap the -e code or the script file in
  `while (<>) {..}` [`continue { print; }`]), `-i[ext]`, `-e code`/`-E code` (the code is not an @ARGV element).
  Also `-l[oct]` (chomp + `$\`), `-a`, `-F pat` (`@F`), `-0[oct|xHEX]` (`$/`), `-x[dir]`, backticks and `qx{}` (`readpipe`, list
  context in `@a = ..` through `readpipelist`), `my @a = <FH>` / `<>` / `` `cmd` `` (`p5_readline_all`). The switches of a script's `#!..perl -nal` line are applied
  (`p2_switches` in `front/p2.c`), a program is read from stdin (`p2 < file`, `p2 -`), `BEGIN` blocks of a -n/-p script are hoisted in front of
  the loop (the `__DATA__` text stays behind it), `*ARGV = *DATA` in a BEGIN block works (`$P5::DATA_ALIAS`), `$.` counts per handle (`<>` over all files).
  Missing: `qx'..'` (no
  interpolation), `-C`, `-s`, `END` inside -n/-p code, `eof` resets, list contexts other than `@a = ..`/print
  (`my ($a, $b) = <FH>`, `foreach (<FH>)`), a NUL in a string (`length` stops at it). `@b = @a` copies, `\@a` is one call argument
  (`p5refid`); `while (<>) {..} continue {..}` works (the loop variable is declared before the test).
- **`wantarray`** (only compiled when the source text mentions it, `p5_want_used`): a sub whose body reads it starts with
  `my $__wa = p5wantget()` (`p5_sub_proto`); the context comes from a global that the call site sets: `$x = f()` (scalar) and a
  bare statement `f();` (void) are wrapped in `p5wantend(p5want(CTX), CALL)` (`p5_want_wrap`, `p5_want_void`); every other
  call is a list call, also in places where perl has scalar context (`f() . "x"`, `if (f())`, `scalar(f())`, ternary conditions,
  args of a scalar call). A sub that does not use wantarray leaves the context set while it runs, so `return g()` inherits it
  (as perl does); an unrelated nested call in the arguments (`$x = f(g())`) sees it too. `@a = f()` wraps a scalar result as a list
  (`p5aslist`). A statement-level call that is the last statement of a sub (`sub w { g(); }`) is void, perl inherits the context.
- **Fresh variable per assignment that creates it**: the first assignment of a local compiles to `OP_NEWLOCAL` (it replaces the
  local instead of writing through the box a closure took), so `for my $n (..) { push @s, sub { $n } }` closes over one `$n` per
  iteration. An assignment whose right side makes a closure keeps `OP_SETLOCAL` (a closure may capture the variable it is assigned
  to). The x86 and arm64 JITs implement it as a plain store; the ppc jit (no upvals at all) maps it to its setlocal.
- **`(EXPR)` is still parsed as a one-element list literal**; `p5_unparen` (`syn/syntax-p5.y`)
  unwraps it (also nested) as the rhs of a scalar assignment, a ternary or `if` condition, `return`
  and the operands of `== != <=> lt..`. Still a tuple (always truthy): `&&`/`||` operands, function
  args. A real fix needs scalar-vs-list context threading.
- **DEBUG builds keep an object start map** (`M->birth_map`/`old_map`, one byte per 8 bytes,
  `pngc_sane_object` in core/gc.c): the conservative stack scan ignores words that are not the start
  of an object (stale words into JIT code buffers looked like vtables). Release builds (DEBUG=0)
  only check alignment and size.
- **Packages as classes** (lib/p5/p5oo.c): `package Foo;` / `package Foo { }` set the parse time
  package (`p5_pkg`, "" is main); `sub name` in it is defined as `Foo::name` and also registered
  by `p5defsub`; `bless REF, CLASS` gives the container (the tuple/table/closure itself) a new
  `uniq` and records the class under it (`p5_class_of`); `$o->m(args)` is `$o.p5meth("m", (args))`:
  a blessed value or a registered package name resolves `m` depth first along `@ISA`
  (`our @ISA = ..`, `push @ISA`, `use parent/base` call `p5setisa`/`p5isapush`), anything else is
  the plain message send (`p5_call_method` fills the signature defaults, as OP_CALL does).
  `SUPER::m`, `can`, `isa`, `UNIVERSAL::isa`, `mro::get_linear_isa`, `__PACKAGE__`, `ref`.
  `our $x/@a/%h` in a package is the global `Foo::x` (`p5_vn`: later `$x` of that package, in
  strings too; only the first variable of an `our` statement, `our ($a, $b)` is not qualified).
  A sub of a package may be called before its definition (`p5_prescan_subs` notes the names, the
  subs are hoisted by `p5_predeclare`); a call of a not yet known bareword stays a string.
  Identifiers: XID_Start/XID_Continue from `core/unicode.c` (`potion_uni_ident_len`, tables from
  libu8ident/rcc, `unicode_xid.h` generated from DerivedCoreProperties.txt) in all three grammars
  (`yy_uni` predicate); `potion_uni_ident_check` (TR39 mixed scripts/confusables) only warns:
  `p5_uni_warn`, `pn_uni_warn` (potion), `p6_uni_warn`. No NFC normalization of identifiers yet.
  Method `AUTOLOAD` (sets `$Pkg::AUTOLOAD`), `@Pkg::ISA` as a global array (also `@{"Pkg::ISA"}`, `push`),
  symbolic `@{"name"}`/`%{"name"}`/`&{"name"}(..)` (`&$name(..)` with a string does not work: a string
  called with args is an index) and operator overloading of `+ - * / % ** ==` (left operand an object;
  `==` only for plain variable operands, `<=>`/`cmp`/`neg`/`bool`/`.`/`x` are not dispatched) are done.
  Missing: DESTROY (the gc has no refcounts), function (not method) AUTOLOAD, `local *glob`.
- **AST node mutation needs a write barrier**: grammar actions that assign
  `PN_SRC(node)->a[k] = ...` must follow with `PN_TOUCH(node)`; the node may have been
  promoted by a GC during the parse, and a young child behind an old parent goes stale
  (segfault in `potion_mark_major`, e.g. re/pat_special_cc.t).
- **References**: `\$x` is a real aliasing object (`lib/p5/p5ref.c`, `$$r`, `${$r}`, `$$r = v`,
  `+=`, `++`, `ref` SCALAR/REF); arrays/hashes/closures are their own references. `\[..]`/`\{..}`
  give ARRAY/HASH instead of REF; `$h{cb}->(..)`, `$a[0]->(..)`, `&{EXPR}(..)` call via `p5callv` (lib/p5/p5table.c; it hands the args to the VM directly because `potion_vm_proto`'s vararg heuristic replaces strings by the numeric @_ sentinel);
  `$$r++` as an expression yields the new value. `(0, @a, 3)`, `(%h)`, `(@$r)` flatten only for
  `@x = LIST` (`p5_flatten_multi`), print lists and `join`, not for call args or `return`. A list or hash held in a scalar stringifies as ARRAY(0x..)/HASH(0x..) (`p5_tostr`), so array values must be flattened at parse time (`p5_arrayish`).
- **BigInt (libtommath, `core/bigint.c`, type `PN_TBIGINT`, `struct PNBigInt`)**: every integer beyond
  the tagged 63-bit range (|n| >= 2**62) is a PNBigInt: sign, limb count and the `mp_digit` limbs inline
  in the GC object (no finalizer; operations view the limbs as a temporary `mp_int`, compute in a malloc'ed
  one and normalize the result: a tagged integer if it fits). Ordinary values never allocate one. libtommath
  (`3rd/libtommath/bn_*.c`, `LTM_ALL`) is part of libpotion/libp2; win64 uses 16-bit digits (`-DMP_16BIT`:
  mingw sets `__x86_64__` but `long` is 32 bit). The VM/JIT integer overflow (`PN_VM_MATH3`, `potion_x86_add`
  now a real `add`, since `lea` does not set OF) calls the number methods under `P2`; p5 (`lib/p5/p5number.c`)
  gives a bigint for results in perl's IV/UV range -2**63..2**64-1 (`~0`, `1<<63`, `hex("ffffffffffffffff")`,
  literals) and a double beyond (`p5_bigres`, perl compatible). `| & ^ ~ << >>` stay 64 bit (signed under `use
  integer`). Bytecode constants: tag 4 = decimal string. p6: integer literals beyond int64 are kept
  exact via a `PVIP_NODE_BIGINT` node (decimal text, `syn/pvip_node.c` strtoll-ERANGE path with
  `mp_read_radix`/`mp_toradix`); the translator (`syn/pvip_to_pn.c`) no longer truncates `->iv` to
  `(int)`. `lib/p6/p6core.c` re-registers `+ - * **` on the number vtables (`p6c_add/sub/mul/pow`,
  exact bigint for integer operands, never a UV clamp or a silent double; libp6 loads only in p6 mode
  so the p5 methods are untouched). `**` is right-associative and takes a signed exponent
  (`signed_exponent` in `syn/syntax-p6.y`; `2 ** -1` used to parse as WhateverCode). Missing:
  potion-level (non-P2) overflow still gives doubles, p6 `/` on bigints gives a double instead of a
  Rat, `int()`/`sqrt` of a bigint go through the double. `do { use integer; EXPR }`
  (and any block with `local`) returned the restored value instead of EXPR for non-`EXPR` last statements
  (`p5_is_value_ast`): fixed.
- **Stringification: `p5_tostr` (lib/p5/p5ref.c) is the one entry point** for turning a value into a
  string in p5 natives (undef is "", booleans 1/"", code refs CODE(0x..), objects their `string`
  method). New types and stringification hooks (overloaded "", tied scalars) go in its switch; never
  `potion_send(v, PN_string)` directly in lib/p5, and never add a `string` method to a core type.
- **References are only partly supported**: `{k=>v}` (anon hash, a table),
  `[..]` (a tuple), `$r->[i]`, `$h->{k}`, chains (`$n->{x}{y}`,
  `$n->{l}->[1]`), `$r->[i] = v` / `$h->{k} = v` (`elemchain`,
  `p5_elem_op`: reads send `at`, the last step of an assignment sends
  `put`), `exists`/`delete` on hash elements (also via `->`),
  `push`/`unshift @a, LIST`, `keys`/`values`, and `@$r`/`%$h`/`@{$r}`
  (the reference IS the tuple/table, so deref is the identity) work.
  Missing: `\@a`/`\%h`/`\$x` real reference values (`\@a` just yields the
  array), `++`/`.=` on elements (slices, slice assignment, `->@*`, `->%*`, `->$#*`,
  `->@[..]`, `->@{..}` work: `slicepair`, `p5sliceset`, `p5sliceof`), `$h->{k}->method`,
  `keys(%h)->length` (a method call on a paren-call result is a parse
  error). `my @a = <FH>` (list-context readline) fails to parse.

- **Coderef calls**: `$cb->(args)`, `$f->(1)->(2)`, `$h{cb}->()`, `f(..)->m`, `f(..)->[i]`/`->{k}` and
  `$cb->call(args)` (p2 only, `p5_meth` fallback) work.
- **`exists`/`delete $a[$i]`** work (`p5exists`/`p5delete`, `lib/p5/p5table.c`); an undef element
  counts as deleted when shrinking the tail, `exists` is only an index range check.
- **Statement modifiers** `for`/`foreach`, `while`, `until` work on
  expression and assignment statements (`p5_forlist`, `p5_while_mod`);
  a bare `/pat/` matches `$_`; `do {...} while` and a bare `print`/`say` (default `$_`) work.
  Gotcha: stmt alternatives are ordered; a modifier alternative must come
  BEFORE the plain `assigndecl sep?` one or that wins and the modifier is
  parsed as a separate statement.
- **`continue` blocks** (`while`/`until`: run at the loop test, so `next` runs them; `foreach`: appended to the
  body, `next` skips them) and **bare blocks with `last`/`next`** (a one-shot `while`, `p5_bare_block`) work.
- **Loop labels** (`OUTER: for ...`, `last OUTER`, `next OUTER`) work for foreach/while/until/C-style for:
  the loop body starts with a `__label_NAME` marker message (`p5_label_stmt`) that core/compile.c turns
  into the loop's label; `break`/`continue` with a label argument jump out of the matching enclosing
  loop (`struct PNLoop.parent`). Not labeled: bare blocks (`FOO: { ... last FOO }`).
  A C-style `for` runs its step at the loop test (first-iteration flag), so `next` does not skip it.
- **p5 builtins** `lc uc reverse substr index rindex join sprintf` exist
  (`core/string.c`, byte oriented, `%d %s %f %x %o %c %g %e`). Natives with
  optional parameters read GARBAGE for omitted ones, so calls to
  substr/index/rindex/join/sprintf are padded with undef to full arity at
  parse time (`p5_pad_args`); add new optional-arg natives to that table.
  Still missing: `uc`/`lc` on non-ASCII, `wantarray`.
- **`map`/`grep`/`sort BLOCK LIST`** and `sort LIST` work (`p5map`/`p5grep`/
  `p5sort` tuple methods in `lib/p5/p5table.c`; the block reads the lobby
  globals `$_`, `$a`, `$b`, pre-created at init). `map`/`grep EXPR, LIST` and
  `sort NAME|$ref LIST` work (the expression / call becomes the block). Missing: list-context
  `scalar(@a)`, nested `$_` aliasing (assignments to `$_` do not write
  back).
- **`scalar(@a)`, `ref`, `ucfirst`, `lcfirst`** exist. `[..]` is
  `LIST->clone` so a one-element `[x]` is not unwrapped by `p5_unparen`.
  `$a[0]->[1]` / `$h{k}->{j}` work. Missing: `exists`/`delete` on array
  elements.
- **C-style `for (init; cond; step)`** is desugared to `{ init; while (cond)
  { body; step } }` (`p5_cfor`): `next` skips the step (jumps to the test).
  Gotcha for grammar authors: an optional `x:rule?` that does not match
  leaves the previous match's value in greg's slot (fixed three times now:
  anonsub, cfor; use an explicit `'' { $$ = PN_NIL }` alternative).
- **`eval`/`die`:** `eval { BLOCK }` (setjmp frame in `potion_p5_eval`,
  `core/objmodel.c`) returns the block value; `die`/`warn` are lobby
  methods, `$@` is a lobby global; an uncaught `die` exits 255. `die`/`warn` without a trailing newline say ` at FILE line N.` (the line of the statement: a statement that mentions them
  starts with `p5setloc`, `p5_with_loc`; no `, <FH> line M`). Missing: `die` with
  an object/list, `$SIG{__DIE__}`, `eval "string"` setting `$@`,
  `wantarray`.
- **`state $x [= EXPR]` / `state @a` / `state %h`** (not `state ($a, $b)`): the name maps to the global `$__st_N_x` until the end
  of the block (`p5_state_decl`, `p5_vn` consults `p5_state_keys`), declared at the top of the program by the
  viv prologue, initialised once by a flag `$__si_N`.
- **`local $x` / `our`:** `local $x [= e]` on a plain scalar is saved in a
  temporary at block start and restored after the block's last statement
  and before a top-level `return` (`p5_local_blockend`); `die`, loop
  `last`/`next`, and a `return` nested in an inner statement skip the
  restore. `local @a`/`%h` (with or without `= LIST`) work, saved as a clone; `local $h{k}`/`$a[i]` save `(existed, value)` (`p5lsave`/`p5lrestore`, plain `$h{k}` only, no `->` chains); `local $_` works. `our $x`/`@a`/`%h` are plain
  lobby globals (no package namespaces).
- **`each` / `keys` / `values`** (`lib/p5/p5table.c`; `p5_each_scalar` / `p5_special_cond` / `p5_bare_unary` in
  `syn/syntax-p5.y`): `each %h` yields the pair in list context, the key (array: the index) in scalar context —
  the parser appends the `p5eachk` native to the chain in scalar contexts (scalar-assignment RHS, `p5_cond`
  conditions, and the RHS of an assignment inside a condition). `while (each %h)` / `while (each %h) BLOCK` /
  `if` / `until` terminate: the condition of `while (COND) BLOCK` / `if (COND) BLOCK` / `UNTIL` is in scalar
  context, `p5_special_cond` scalarizes a one-item condition list through `p5_cond` (`@a` → its length,
  `()` → undef, `keys %h` → the count, `each %h` → the key); `foreach` / `for` are not touched (their list is
  the iterated list). `keys %h` / `values %h` also reset the each iterator of the hash (perl semantics; libp5
  overrides the core table `keys` / `values` with `p5_keys` / `p5_values`, a tied hash keeps its own vtable's
  TIEHASH FIRSTKEY/NEXTKEY). A bare `each` / `keys` / `values` with no operand dies with
  "Not enough arguments for X"; the other named unary operators still operate on `$_`, `shift` / `pop` on `@_`.
  op/each.t: 43/57; the rest are `keys %h = LIST` (resize), the deprecated bareword `keys(hash)` / `each(h)`
  forms, `scalar %h` "N/M" bucket stats, and UTF-8 / EBCDIC hash-key semantics.
- `python3 test/sweep5.py`: 184 of 482 files pass; the rest are parse errors, library limits of
  `test/p5/test.pl`, missing files (`./TEST`, `t/test.pl`, `thread_it.pl`) and the still-hanging files.
- Beyond that, the remaining majority of failing files are
  architecturally the same situation as roast6's parse-error bucket: a
  long tail of individual p5-grammar gaps. Sweep (run from `test/roast5`
  with `-I../p5`, 482 files, ~32 exit 0): the biggest buckets are now
  `mro/*` (~50 files: `package Foo; use base ...`, `mro::get_linear_isa`,
  `eq_array`, real packages/inheritance), `io/*` (layers/bom: segfaults or
  rc=1, ~50), `sub name ($;$) {` prototypes, `no warnings ...;` / `chdir
  't'; require ...` in odd positions, heredocs inside interpolated
  constructs, `<DATA>`/`<>`. Use the TAP-scanner +
  stderr-bucketing approach above (bucket by the `before text "..."`
  prefix) to find the next highest-frequency one rather than guessing.

- Support PNSyntax objects, the state of a compiled and shared syntax.y greg file.
  A syntax should be extendable by user-defined macros (not evaluating its args),
  which override functions in the syntax order.
  See cperl:pod/perlcperl/pod:Macros and cperl:t/comp/macro.t for p5.
  The syntax should also be mapped to P6Grammar (ie p6 grammar) objects,
  which need to create .y files, and auto-compiled to a shared lib.
  See https://docs.raku.org/language/grammars

- **compile/c backend** (`core/compile-c.c`, `core/compile-c.h`, `lib/potion/compile/c.pn`): `--compile=c` emits one C
  function per Proto (labels + gotos, array ABI `PN_C_F`, stored in `PNProto.cfunc`, closures use `potion_c_proto`) and rebuilds
  the proto metadata at startup; no VM/JIT runs the code. `main` unless `-DPOTION_MODULE`; `-DPOTION_MODULE_NAME=X` renames
  `Potion_Init_X`; exported `potion_compiled_run`. Test: `make test.compile-c`. Open: `exe` backend (no automatic cc call),
  `OP_TAILCALL`, constants other than ints/strings/doubles/bigints/tuples fail the emit, C protos cannot be dumped as bytecode,
  `callcc` continuations and the `-d` debugger do not see compiled frames, p5 `p5callv` packs >12 args only for `$__a0` subs.

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
