## ~ readme ~

```
         .ooo
          'OOOo
      ~ p ooOOOo tion ~
          .OOO
           oO      %% a little
             Oo    fast language.
            'O
             `
            (o)
        ___/ /
       /`    \
      /v^  `  ,
     (...v/v^/
      \../::/
       \/::/
```

p2 is a she. She is a perl5 and possible perl6/nqp backend, based
on why the luck stiff's potion. See `README.potion.md` why potion is exciting.
Homepage: http://perl11.com/p2/

| Command                     | Time      |
| --------------------------- | --------- |
| `parrot example/fib.pir 28` | 0m1.746s  |
| `perl example/fib.pl 28`    | 0m0.439s  |
| `p2 -B example/fib.p2 28`   | 0m0.177s  |
| `potion example/fib.pn 28`  | 0m0.013s  |
| `p2 example/fib.p2 28`      | 0m0.013s  |
| `perl example/fib.p2 28`    | 0m0.196s  |
| `raku example/fib.p6 28`    | 0m0.569s  |
| `p2 example/fib.p6 28`      | 0m0.018s  |
| `parrot example/fib.pir 40` | 3m36.447s |
| `perl example/fib.pl 40`    | 2m19.752s |
| `p2 -B example/fib.p2 40`   | 1m1.940s  |
| `potion example/fib.pn 40`  | 0m3.063s  |
| `p2 example/fib.p2 40`      | 0m3.105s  |
| `raku example/fib.p6 40`    | 1m29.872s |
| `p2 example/fib.p6 40`      | 0m3.086s  |

For most small examples comparable performance to lua and C. Mostly 10x faster than perl5, and
~200x faster than raku.
For bigger examples optimizable via static types/early binding.

Its exciting points for perl folks are:

- Radically simplified and functional core.

- Should parse most of perl5, just some XS and B tricks not.

- Signatures, fast mop, types, native threads, call-replacing
  macros for perl5.

- No worries about pseudo-scientific discussions, project management,
  performance bugs and feature deadlocks. Leaving the slack behind.

- An AST tree matcher (`potion_ast_match`, `potion_ast_find`, `potion_ast_walk` in `core/compile.c`):
  patterns are ordinary source trees with wildcards, used for the compile-time
  optimizations of the parsers (e.g. `my ($a, $b) = @_;` becomes plain parameters).

- A lightweight generational GC, based on Basile Starynkevitch's
  work on Qish, with ~4ms per GC on average with < 100MB heaps.
  <http://starynkevitch.net/Basile/qishintro.html>
  The compiler will add hooks for objects leaving scope and add
  `DESTROY` method calls if they are visible at
  compile-time. Run-time created `DESTROY` methods will not be
  automatically called (yet).

- Using most of Damian Conway's `p5i` recommendations:
  i.e. classes are immutable and final by default.
  See <http://web.archive.org/web/20040207072905/http://www.yetanother.org/damian/Perl5+i/>

- `libp2` is usable as a fast backend for nqp/perl6, but parsing and supporting
  p6 natively without going through nqp and the rakudo bootstrapping process
  should be easier and faster.
  Tokuhiro Matsuno already wrote a p6 parser, `pvip`.

- Native and fast calling convention, no XS stack args on the heap.
  XS is replaced by extern (dynamic ffi), and/or shared libraries
  with normal function calls into the vm.

- Just-in-time compilation for x86, x86-64, ppc and arm (aarch64) done.

- Intermediate bytecode format and VM. Load, dump and compile code, to .c or
  exe for all supported syntaxes: potion, p2, p5, p6.
  Decent speed and cross-architecture. Heavily based on Lua's VM.

- int, str, num are objects, you can optionally type all args and
  lexical variables. In fact everything is an object as in parrot,
  even the parsed AST, the vm state.

- Sized arrays are non-autovivifying and initialized with undef,
  resp. if typed 0, 0.0, "".

  ```perl
  my $a[3]; print $a[3]    # compile-time error: array out of bounds
  my int $i[3]; print $a[2]  # 0
  my num $n[3]; print $n[2]  # 0.0
  my str $s[3]; print $a[2]  # ""
  ```

- A new pragma `no magic` applies to all visible objects in scope.

  ```perl
  {
    no magic;
    use Config;
    print $Config{'ccflags'};
  }
  # compile-time error: Invalid use of tie with no magic
  ```

  use

  ```perl
  {
    no magic;
    use Config ();
    print $Config::Config{'ccflags'};
  }
  ```

  instead.

- p6model: via scoped mixins, :multi, roles. Like Moose, just better
  and roughly estimated 800x faster.
  No multiple inheritance yet, `@ISA` == 1.
  Only one parent, depending on another parent, rather multiple
  mixin compositions.

- `~~` match works because all data are typed objects.
  A destructuring-bind alike new match operator which matches
  structures (list, trees, hashes of hashes) against structures,
  values and types.

- Bootstrapped "id" object model, based on Ian Piumarta's soda
  languages. This means everything in the language, including
  object allocation, parser, compiler and interpreter state are
  part of the object model. (See `COPYING` for citations.)
  In fact all objects and classes are closures, which simplifies
  scoping, and makes p2/potion a functional vm.

- parsers are scoped, available as object and run-time compilable and
  loadable for ffi, p6, and other languages. p6 grammars and p2 macros
  are implemented that way.

- Interpreter is thread-safe and reentrant. This should
  facilitate parallel interpreters initialized in a threadpool.

- Small core. Under 10kloc. Install sloccount and run: `make sloc`.

- Reified AST and bytecode structures. Wrote a C compiler and interactive
  debugger with this.
  This is very important to me. By giving access to the parser and compiler,
  it allows people to target other platforms, write compilers, debuggers,
  code analysis tools and even fully bootstrapped VMs. I'm not as concerned about
  the Potion VM being fully bootstrapped, especially as it is tied
  into the JIT so closely.

- Memory-efficient classes. Stored like C structs. (Although the
  method lookup table can be used like a hash for storing arbitrary
  data.)

- The JIT is also used to speed up some other bottlenecks. For
  example, instance variable and method lookup tables are compiled
  into machine code.

- Fast asynchronous IO via `libuv`

However, some warnings:

- Ints cannot hold pointers and need to be boxed. This should be
  transparent though. Use WeakRefs for pointers.

- Strings are immutable (like Lua) and byte arrays are used for I/O
  buffers. All strings and symbols are utf8 internally.
  Identifiers in the potion, p5 and p6 grammars are checked against Unicode XID_Start/XID_Continue,
  and warned about per the TR39 security profile (mixed scripts, confusables, as in cperl, rcc,
  java and rust) via libu8ident (`core/unicode.c`).

- Limited platform support for coroutines.
  This affects exceptions.

- No regular expression bindings yet (`pcre` or `sregex`)

- No OS threads yet.

- No signal handling yet.

- The compiler is mostly good enough, as in lua, but there are no
  tradional compiler optimizations yet. We'd might need to add
  constant folding and propagation, simple local basic block
  optimizations, peephole optimizations, loop optimizations,
  tracking side-effects. And tailcalls are not yet detected.

- No type checks, bounds-check elimination and type optimizations
  yet.

## ~ compiler backends ~

`--compile=BACKEND` (`potion` and `p2`) writes the program in another form:

- `bc` (default): serialized bytecode, `foo.pnb` / `foo.plc`, run by `potion` / `p2`.
- `c`: readable C source, `foo.pn.c`. Every function of the program is compiled to a
  C function, so no bytecode interpreter or JIT executes it. Compile it with `POTION_CC`
  to a standalone executable (it has a `main`), or with `-DPOTION_MODULE` to a loadable
  module, linked to libpotion, or libp5 / libp6 with libp2. See `doc/start.md`.
- `exe`: an executable, `foo.pn.out`. The serialized bytecode is embedded as a byte
  array in a temporary C file, which is compiled on the fly and removed again. The
  generated C is not meant to be read. It loads without parsing and runs with the JIT
  (or VM). Run it from the checkout root or an installed prefix, so that headers
  and libraries are found.

## ~ benchmarks ~

`test/bench-summary.sh` (`RUNS=3` for the best of three). Wall clock seconds; the `exe`
columns are `--compile=exe` builds. `x` is the time relative to `pn` for the pn column,
to `perl` for the pl columns and to `raku` for the p6 columns.

| program       | pn    | pn exe (x pn) | pl p2 (x perl) | pl perl | pl exe (x perl) | p6 p2 (x raku) | p6 raku | p6 exe (x raku) |
|---------------|-------|---------------|----------------|---------|-----------------|----------------|---------|-----------------|
| ackermann     | 0.012 | 0.013 (1.1x)  | 0.023 (3.8x)   | 0.006   | 0.010 (1.7x)    | 0.052 (0.1x)   | 0.374   | 0.050 (0.1x)    |
| fib           | 0.017 | 0.015 (0.9x)  | 0.044 (0.2x)   | 0.205   | 0.035 (0.2x)    | 0.034 (0.1x)   | 0.623   | 0.022 (0.0x)    |
| fib.p2        | 0.017 | 0.017 (1.0x)  | 0.027 (0.1x)   | 0.211   | 0.021 (0.1x)    | -              | -       | -               |
| nqueens       | 0.013 | 0.011 (0.8x)  | 0.092 (8.4x)   | 0.011   | 0.055 (5.0x)    | 0.039 (0.1x)   | 0.397   | 0.032 (0.1x)    |
| spectral-norm | 0.210 | 0.203 (1.0x)  | 0.302 (1.6x)   | 0.191   | 0.254 (1.3x)    | 0.492 (0.2x)   | 2.155   | 0.408 (0.2x)    |

The runs are very short, so the ratios are noisy.

## ~ debugger ~

There is now a full interactive debugger with readline, to evaluate and inspect all internals at runtime.
See [doc/debugger.md](doc/debugger.md).

## ~ building and installing ~

```sh
make
make install
```

Look inside the file called `INSTALL.md` for options.

To debug the interpreter (gdb sessions, the AST and bytecode dumps, how globals, mixins and
control keywords work, the threading model) see `doc/INTERNALS.md`.

## ~ feverish and fond thankyous ~

why the lucky stiff left the ruby community. potion was his last
public project. It's a work of great art and mastership. I could not
think of anything better. I think it is better than Go, if potion
would have had added auto-threads and channels.

Rob Pike for Go, and the parrot community for parrot.

Larry Wall for perl5. We miss his leadership and technical excellence.
Without him nothing got done, and if something was done it was wrong.
Besides defined-or, which left Tom Christiansen behind.

why the lucky stiff thanks:

why is gravely indebted to Basile Starynkevitch, who fielded
questions about his garbage collector. why favors French hackers
to an extreme (Xavier Leroy, Nicolas Cannasse, Guy Decoux,
Mathieu Bochard to name only a portion of those he admires) and
is very glad to represent their influence in Potion's garbage
collector.

Matz, for answering why's questions about conservative GC and
for encouraging him so much. Potion's stack scanning code and
some of the object model come from Ruby.

Steve Dekorte for the Io language, libgarbagecollector and
libcoroutine -- He referred frequently to all of them in
sorting out what he wanted.

Of course, Mauricio Fernandez for his inspiring programming
journal housed at http://eigenclass.org/R2/ and for works
derived throughout the course of it -- extprot most of all.
Many of my thoughts about language internals (object repr,
GC, etc.) are informed by him.

Ian Piumarta for peg/leg. We use a re-entrant customized version of
it, but the original library is sheer minimalist parsing amazement.

Final appreciations to Jonathan Wright and William Morgan
who pitched in, back in the wee hours of Potion's history.
Thanks.

## ~ license ~

See COPYING for legal information.
potion is MIT licensed, which lets you do anything you want with this.
p2 and perl6 code is ARTISTIC 2 licensed.
