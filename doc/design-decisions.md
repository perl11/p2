# design decisions on p2

With p2 I plan to parse and execute perl5 asis.
libp2, the compiler and vm based on potion, should be a good target
for perl6.

But I will not be able to run 100% of CPAN. I could, but then there
would be no progress.
I plan significant enhancements in perl performance and features.

## Generally

I go along with \_why and every lisp coder. Good software should be
beautiful. But not too beautiful. Code is also art not only technology.
Short, precise, readable. No one wants to work with a big and ugly
mess, not even companies. Too much code smells. Rather restrict the
usage to those who know and can be taught, than sacrifice your system
for it. On the other hand too beautiful code rarely gets out of a niche.
Worse is better.

Support 90% but do not sacrifice for the rest.
gmake and gcc/clang are available everywhere, even on pure bsd’s.

Examples:
no support for MS cl/nmake. people should use mingw with gmake and gcc instead.
no support for BSDmakefile syntax. see the `bsd` branch.
no support for pure strict c++ compilers. see the `p2-c++` branch.
no vax, hpux, aix without gnu tools.

# Incompatibilities

Some functionality will change, some annoying bugs fixed,
and some functionality might get removed, or not yet supported.

## XS

Problem will arise with XS code, since the VM is different, and not
all XS API functions can be supported. It should be much easier to
use XS-like functionality with the new FFI, or by using extension
libraries with native calls. See `lib/readline`. So we will loose
40% of CPAN code, but will win on performance, expressibility and
compile-time error checking when binding libs.

There should be a translator of old XS code to check the stack
(argument + return values) handling macros and convert them to direct
calls.

## functional programming

p2 is pretty obscene in being a pure functional language, certainly more functional
than LISP.
Internally any non-lexical variable (GV in perl5) is a function, a closure, which means
it is an object, which means getting the value is done by sending it a message with
an empty name,
and setting the value is done by sending it the “def” message.

(msg %ENV) =\> return value of %ENV
(msg %ENV “def” &env) =\> set new value of %ENV

same for keys (accessor support):

(msg %ENV key) =\> return a %ENV element
(msg %ENV “def” key value) =\> set new value of a %ENV element

Yes, you smell Smalltalk.
This is needed to be able to support a proper object system, types and esp.
proper multi-threading. There are special MOP methods for default
readers and writers, type_call_is: key, type_callset_is: key+value.

On the user-side some side-effect-only functions will be changed to return the
changed argument.
E.g. chop, chomp, …
return \$s =~ s///r as default if the left hand side (wantarray) is no list.

The parser simplier and different. All statements return values. Everything can be on a
right hand side of something.
E.g. **if** returns the value of the executed branch or
**undef** if no branch is choosen.

    {
        $a = if (1) { $c }; #same as: $a = $c;
    }

## order of destruction

If you don’t use explicit DESTROY calls at the end of blocks, the compiler
might miss some DESTROY calls of untyped objects. DESTROY might be called
later then, as in other GC’d languages. get over it.

reference counted objects are too dangerous, too hard and too slow.
cyclic data structures do not play well with refcounts.
use-after-free bugs are by factor 10 the most exploited security problems
nowadays, and perl5 is full of them.

## lexical hash iterators

iterating a hash twice in lexically scoped blocks does not work in the second,
outer iterator, as the iterator in p5p perl is stored in the data.
This will be changed to be stored in the scope (block).

i.e. using Data::Dumper inside a each %hash loop will restore the position after
Data::Dumper dumped the hash.

# New features (planned)

### All data are objects, all declarations can be optionally typed.

extendability, maintainance

efficient oo and dynamic type system, with compiler support for static types.

### const declarations for lexical data, @ISA, classes and functions/methods

efficiency

Also needed for threads and oo to avoid generating writer methods.
Define immutable and final classes.

### optional function signatures and type declarations

efficiency and safety, compile-time checks

### efficient meta-object system, with classes, methods, roles

like Moose (i.e. CLOS), but ~800x faster and with native type support.
i.e. compile-time checks.

### sized arrays

efficiency

### no magic

efficiency

### match operator

expressibility

A proper matcher should be able to match structures and types, and to
bind result variables.

### dynamic and cleaned up parser

maintainability.
new technology (risc and code maintainability), but needed for macros.

allow sensible language features, disallowed by p5p or the old yacc
parser. the parser grammar needs to be expressive, even for perl,
which is known to be hard to parse, and dynamic at parse-time
(prototypes).

new syntactic constructs needs to added to the grammar, not elsewhere
in the code. the parser needs to be accessible and extendable at
compile-time, maybe even run-time, but we cannot use a non-optimized
pure top-down parser as e.g. lua to enable this.

we need to precompile the base grammar, bootstrap system macros and
allow user macros. expose the parser API to the user. query and
insert rules. use custom rules to parse ffi declarations (c headers)
templates, … or even basic, ruby, python or perl6.

    {
      use v6;
      # perl6 syntax...
    }

*(this will need a pre-compiled `syntax-p6.g` and scoped syntax)*

Either done by extending packrat greg (ie leg) by with a parser
interpreter to add rules at run-time, - from precompiled rules and
user-added rules - or by extending marpa to be extensible.

### macros as parser extensions

expressibility (lisp-like)
keep the vm small, do not prototype everything in the C library.
use the existing parser engine.

macro args are rules (non-terminals) and terminals (strings) to be
added to the parser, the macro block is evaluated at compile-time,
with \`…\` expanded at run-time.

so perl will be the first non-lisp like language with a proper macro
system, i.e. extending parser grammars. perl6 has similar ideas using peg
at run-time, but their syntactic macros are too complicated for me.
maybe using the perl6 `<rule>` syntax looks ok. *(i.e. `<block>` below)*

There are some similar non-mainstream approaches, on mono or java or
haskell, but none on fast, compiled to C scripting languages.

    syntax-p5.g:
        block = '{' s:statements* '}' { $$ = PN_AST(BLOCK, s); }

     macro ifdebug block 'ifdebug' {
      if ($DEBUG) `block`;
    }
    { call() } ifdebug;

### auto-threads

the p2/potion data-structures, compiler, vm, gc, interpreter are
thread-safe.

with a OO task and scheduler interface as in parrot (pre-create,
useful esp. for windows alarms),
or as in Go (similar),
or maybe only non-autothreaded as in lua, with just first-class
coroutines, methods: create, status, resume, yield, wrap

### ffi

builtin
extendability, maintainance

maybe ctypes alike, maybe something simplier or more advanced.
(i.e. import parses header files and generates all ffi functions
and types, or on the hand only support the bare cc types:
signed/unsigned, void/byte/short/long/ptr float/double/long double)

### method cache

JIT_MCACHE is never actually defined, so it's effectively disabled,
and the deeper reason seems to be that mcache stores raw method
closure pointers as immediates, which breaks under a moving GC since
those pointers would go stale—it's disabled until method weakrefs can
live in fixed memory. I'm also looking at how PN_MCACHE_FUNC's hash
parameter interacts with the x86 compiler internals.

There's a subtler risk too: the cache keys off PN_UNIQ of interned
name strings, so if uniq values ever collide between different method
names, the cache could return the wrong method entirely—the lookup
fallback does an exact pointer compare, but the fast path trusts uniq
alone. I'm also checking the generated x86 jump offsets (jnz distances
differ between 32-bit and 64-bit mov-imm/ret sequences) and confirming
the cmp opcode selection picks the right register width per
architecture. Finally, I'm noting that when a method is redefined, the
mcache memory gets overwritten in place to stay fresh.

A safer alternative is to have the cache return a hash-table index
instead of a raw pointer, so lookups go through kh_val and never embed
GC-unsafe pointers directly — though I need to check whether the
method table changes (puts, deletes, rehashing) elsewhere without
regenerating the cache,since that would invalidate indices too. I'm
also weighing whether this index approach is actually faster than the
straightforward hash lookup.

I should verify collisions by checking kh_key matches before trusting
a cached index, falling back to a full kh_get if not — this handles
the case where PN_UNIQ keys collide and the cache would otherwise
return the wrong entry. I need to check how PN_UNIQ works for
non-pointer keys, and trace how potion_lookup/bind calls into the JIT
code in vm-x86.c alongside the GC and potion.h definitions.

Since mcache only stores stable UNIQ hashes rather than raw pointers,
the keys should be safe fromGC movement — only cached closure values
are at risk. I'm also realizing the cache index only needs
regeneration when kh_put inserts a genuinely new key (ret != 0), not
on redefinition of an existing method, since redefinition keeps the
same index.

Now I'm wondering whether PN_UNIQ for strings could collide, whether
vtables ever get freed (leaking their mmap'd mcache), and how
potion_mmap handles the 4096-byte allocation per class.

Thinking through the fixed-size tradeoff: at roughly 14 bytes per
method on x86-64, 4096 bytes supports about 290 methods before
falling back to NULL. Larger vtables like string or tuple classes
could exceed that, so I'm considering whether to munmap/remap with an
exact size each time, which would require tracking the allocated
length — possibly by adding a new field to the vtable struct, though
that raises questions about affecting GC sizing and whether a 4096
cap is even acceptable for core types with many methods. Given a long
linear compare chain scales worse than a hash for large method
counts, I'm leaning toward keeping the 4096 cap as an implicit limit
rather than over-engineering, and just implementing it as-is to
measure real performance before optimizing further. For the assembly
itself, I'm working out how to encode the miss case correctly on
x86-64, since returning -1 in rax needs a movabs-style immediate load
rather than the zero-extending 32-bit mov used for hits.

An alternative is to embed raw closure pointers directly as immediates
in the JIT code, letting GC patch them via forwarding and registering
mcaches as roots. I need to weigh this against my index-based
approach — considering the cost of GC rewriting machine code
immediates on every collection cycle, requiring writes into W^X pages
and icache flushes for every vtable with an mcache. That's a lot more
involved than it looks: each architecture needs different handling
for its immediate encoding (x86 imm64, ARM movz/movk sequences, PPC
lis/ori), which means maintaining a per-arch side table of patch
sites. There's also the complication of young closures moving during
every minor GC until promoted, the weakref semantics the original
TODO mentioned, and the need for icache flushing on ARM after each
patch — though the mcache keeping closures alive as strong roots
isn't actually a new problem since methods are already kept alive
elsewhere. By contrast, my index approach needs zero GC interaction
and just adds one extra load plus a pointer check, since the table is
already traversed and forwarded by GC anyway — the cost works out to
roughly the same as a hash probe would take. Honestly, the
pointer-patching proposal is workable but meaningfully more complex
and riskier for the same performance win, since the index approach
already skips the hash probe on hits and skips the lookup entirely on
misses.

I'm not seeing a real advantage to the patching idea — the extra
memory load it saves isn't meaningful since the compare chain is O(n)
regardless. I'll stick with the index design and tell the user that
directly, but I also noticed the build output looked off (something
about "rm syn/syntax-p6.c" in stderr), so I should check whether the
build actually succeeded before moving forward with implementation.

No, I'd stay with returning an index from the JIT and leaving the
closure pointers out of the code.

  ┌───────────────┬────────────────────────────────────────────┬─────────────────────────┐
  │               │ Patch the closure immediates on GC         │ Return the khash bucket │
  │               │                                            │ index                   │
  ├───────────────┼────────────────────────────────────────────┼─────────────────────────┤
  │ GC changes    │ Every vtable's mcache must be a root. Each │ None. The methods table │
  │               │ minor and major GC rewrites immediates     │ is already marked and   │
  │               │ inside executable pages.                   │ forwarded, and the JIT  │
  │               │                                            │ code holds only the     │
  │               │                                            │ 32-bit PN_UNIQ and the  │
  │               │                                            │ bucket index.           │
  ├───────────────┼────────────────────────────────────────────┼─────────────────────────┤
  │ Arch-specific │ I'd need a patch-site list per arch. x86   │ None.                   │
  │ patching      │ has an imm64 mov. ARM64 has four movz/movk │                         │
  │               │ words. PPC has lis/ori.                    │                         │
  ├───────────────┼────────────────────────────────────────────┼─────────────────────────┤
  │ Cost per GC   │ Each patch needs a potion_jit_write_protect│ Zero.                   │
  │               │ toggle (a real cost on macOS arm64). ARM   │                         │
  │               │ and PPC also need an icache flush per      │                         │
  │               │ patch. Young closures move on every minor  │                         │
  │               │ GC, so this repeats often.                 │                         │
  ├───────────────┼────────────────────────────────────────────┼─────────────────────────┤
  │ Failure mode  │ A missed patch site gives a wild jump or   │ A stale index is caught │
  │               │ a stale method, and that bug is very hard  │ by the kh_key(...) ==   │
  │               │ to debug.                                  │ key check and falls     │
  │               │                                            │ back to kh_get.         │
  └───────────────┴────────────────────────────────────────────┴─────────────────────────┘

The index approach also fixes a correctness hole that the patching
idea leaves open. PNUniq is a random 32-bit value, so two keys can
collide, and today's chain would return the wrong method. With the
index, potion_lookup compares the key at that bucket and falls back
to kh_get on a mismatch. Plan, same as I was about to do:

1. PN_MCACHE_FUNC returns an unsigned bucket index, or ~0u for "no entry
   with this uniq".
2. On a miss, potion_lookup returns NIL straight
   away. On a hit, it checks the key and then reads kh_val.
3. Regenerate the cache only when kh_put inserts a new key. Redefining
   an existing method keeps the same index.
4. Replace the never-defined JIT_MCACHE guard with POTION_JIT_TARGET,
   as ivars already does. Add a NULL check on the executable allocation.
5. Implement mcache for x86 (shared with the ivars encoding), AArch64
   (32-bit cmp w0, since the upper bits of x0 are undefined) and PPC32.
   Remove the target.mcache = NULL override in vm.c.
