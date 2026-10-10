# YAPC::NA June 2013

\<pre style="background:#AE6;width:25em;float:right;padding-right:2em"\> .ooo 'OOOo ~ p ooOOOo 2 ~ .OOO oO %% a fast perl5 Oo 'O \` (o) \_\_\_/ / /\` \\ /v^ \` , (...v/v^/ \\./::/ \\::/ \</pre\>

Design decisions on p2

**rurban** - Reini Urban \<br\> cPanel, Houston.pm

<http://perl11.org/p2>

# p2 ?

What the heck

# perl11

Ingy: perl 11 = 5 + 6

# perl11

Ingy: perl 11 = 5 + 6

Reini: 1 + 1 = 2

# perl11

perl5 is in stagnation and denial

# perl11

perl5 is in stagnation and denial

parrot is at a dead end

# perl11

perl5 is in stagnation and denial

parrot is at a dead end

but we still want and need what parrot promised

# perl11

a fast and modern perl

# perl11

**a** perl

not perl5 here and perl6 there

# perl11

combine it

# perl11

parse both

hence perl11

# perl11

not all of perl6 is attractive

use a mix

      use p2;

# History

Someone wanted compile to the JVM in 2002 via B::CC but gave up. "It is a dead end." Nonsense.

The perl5 guts became unmaintainable.

Anyway, parrot was born. A register machine, *"faster and better than the JVM"*

Dynamic syntax, dynamic types, dynamic ops

# History

2012 someone called parrot dead (*very wise idea*) despite getting native threads without blocking GC.

The parrot calling convention is de-optimized, mainly by mismanagement. With Dan around it was fast.

Summer 2012 Liz & Wendy organized a perl 5+6 meeting in Perl, and the Oslo Perl Mongers a p5-mop meetup in Stavanger.

I almost gave up on parrot and thought about LLVM, vmkit.

# History

Will Braswell gathers a perl11 group for the forthcoming Austin 2013 YAPC: "Optimized perl"

Ingy, Reini, Flavio Glock and several others meet up in Austin and via Hangout.

Three interoperable parts: parser, compiler, vm/run-time.

# History

Ingy is the parser champ, I am the compiler champ.

Larry already started working on STD_p5 in the summer. Flavios Perlito source to source translator looks wonderful, but we still need our own VM.

v8, parrot, perl5, nqp, jvm, .net, vmkit (LLVM + java GC), ... ?

# History

Orlando January 2013: Stevan Little is angry no immediate p5-mop integration, started **moe**, p5-mop in Scala.

I took why's potion, named p2. Something more realistic.

# potion

abandoned project.

Loosely maintained by a group of post-why folks.

No 0.1 released yet, but some enhancements. (class, dlload)

# potion

PEG style parser. Similar to perl6, contrary to perl5. Enhanced to `greg`.

LUA-based vm

IO/soda based objmodel (the MOP)

Self-written JIT for intel and power-pc. arm missing, but easy.

# potion

Everything is an object, every object is a function.

A functional language, lambda calculus

Every variable is lexical, globals with uppercase first char

# potion

Dynamic, efficient and small to the extreme.

It is beautiful and makes fun to develop again.

# beautiful

Good software should be beautiful. Code is also art, not only technology.

Short, precise, readable. Too much code smells.

# But not too beautiful

On the other hand too beautiful code rarely gets out of a niche.

Worse is better.

Sometimes. Not yet.

# Incompatibilities

Not 100% of CPAN, maybe 70%.

Significant enhancements in perl (5+6) performance and features.

# Incompatibilities

XS

functional programming support

order of destruction

lexical hash iterators

# XS

Macros to access perl stack values

# XS

p2 tags and boxes its values also, but it can call C functions natively

and vice versa, callbacks to p2 are native.

"FFI is builtin"

# XS

Summary: Much easier to write library bindings than with XS.

Call the functions natively, with some stack and boxing macros.

Back-compat XS macro support should be possible. Maybe 70%

The rest should use the new Inline::C like features.

# functional programming support

With p2 every statement returns a value.

# functional programming support

With p2 every statement returns a value.

The returned value needs to be a copy, not a reference. (Efficiency)

      chomp

# functional programming support

less statements, even more expressions.

if returns the result of the executed block

      $a = if (check) { 1 } 
           elseif (check($a)) {-1 }
           else { 0 };

# order of destruction

GC vs. Reference Counting

Finalizers vs. destructors

When to call DESTROY blocks when objects leave scopes. Immediately or deferred. parrot tried both.

A p5+p6 vm must support both.

# lexical hash iterators

Need to fix design bugs

fix each

E.g. Data::Dumper used within each will not reset the traversed key to the last key.

# Features (1/2)

\- all data are objects.

\- efficient meta-object system, with classes, methods, roles

\- ffi

\- function signatures and types, data optionally typed, const

\- sized arrays, no magic

# Features (2/2)

\- match operator (destructuring-bind and \`\` for qq expansion)

\- dynamic and cleaned up parser

\- macros compile-time call

\- auto-threads / coro

# parser

radical simplified parser

no keywords, no lexer.

if is no keyword, just a special compiler macro, as else, while, for, foreach, ...

# parser / compiler

compile-time special cases in the compiler, on the AST level. Not the parser.

--compile=jvm, --compile=c, --compile=exec, --compile=js, --compile=dart

# signatures are parsed

special parser entry points.

at init-time.

parameters filled in and checked at compile-time (types, arity, by ref or by copy) and run-time (defaults, slurpyness).

Same code reused for the FFI
