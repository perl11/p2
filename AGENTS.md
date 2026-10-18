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
  the parser (`p5_fh_note`), else `print lc "X"` would be a handle. Missing:
  `eof` without args, `<>` is STDIN only (no @ARGV files), `DATA` is a temp file bound by the first statement, `binmode`/`seek`/`unlink`,
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
  `pos($s)`. `s///` stores the new string and its value is the count (`p5subcount`); a replacement
  with `$`, `@`, `\\` or `/e` is a closure run per match (`p5substf`, the text is parsed with
  `p2_parse`); `(my $t = $s) =~ s///` works. Still missing: `\U \L \u \l \Q \E` in strings and
  replacements, `/ee`, `/r` on `s///`, `pos($s) = N`, `\G`, `split` with captures, `$/`, `${^OPEN}`,
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
  call site fills defaults from `protos[0]` only. There is no call stack yet:
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
  `test/roast5.sh -e bin/p2 <dir-or-files>`; `make test.p2` runs `OK_ROAST`.

- **Strings and blocks (p5):** `"\Q..\E"`, `\U`, `\L` wrap the pieces of a double quoted string in quotemeta/uc/lc (markers in
  `P->dqpieces`, `p5_dq_finish`); `\u`/`\l` (one character) are not done, `\u` is the 4 hex digit escape. `{ BODY } continue { CB }`
  on a bare block is a once-loop (`p5_bare_cont`); a `return` inside a continue block returns from the step only (same for
  `while ... continue`). `sub NAME(&@)` is noted by the line pre-scan (`p5_blocksubs`) and `NAME BLOCK LIST` becomes
  `NAME(sub BLOCK, LIST)`: only subs defined with the prototype at the start of a line, no imported List::Util `first { }`,
  other prototypes stay ignored.
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
  methods, `$@` is a lobby global; an uncaught `die` exits 255. Missing:
  ` at FILE line N.` suffix (a missing newline just gets "\n"), `die` with
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
- `python3 test/sweep5.py`: 104 of 482 files pass (81 run but print `not ok`, mostly library limits of
  `test/p5/test.pl` and missing files like `./TEST`; about 20 need a missing `t/test.pl`/`thread_it.pl`; the rest
  are single parse errors, e.g. a `%{Pkg::} = ()` stash, `sub run_tests` bodies in op/index.t).
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
