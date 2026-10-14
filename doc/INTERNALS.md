
# ~ the nature of potion's insides ~

Potion's internals are based around the idea
that things that happen frequently should be
given the greater attention.

For example, code execution happens more often
than code parsing and loading. Therefore, the
VM is larger and faster than the parser and
compiler.

Other things that happen frequently:

 * Math.
 * Conditionals.
 * Method dispatch.
 * Local variables.
 * Strings, to build and compare.
 * Memory allocation.

More sporadic things likely include:

 * New methods.
 * Creation of classes.
 * Very deep scoping situations.
 * Altering the inheritance tree.
 * Evals.
 * Code allocation.

So understand that often the choices made in
Potion's VM are designed to accomodate the
first set rather than the second.

#                   ~

This doc is going to touch quite a bit on
the JIT, compiling straight to x86 and PowerPC
instructions. So many languages are targetting
the JVM and .Net these days, or try to use 30MB
LLVM just for adding jit support. It made me
wonder what the hurdles are to generating machine
code directly.

Clearly, the biggest benefit of having a common
runtime is garbage collection. The JVM and .Net
have some wild GC algorithms built in.  And
that's actually a huge benefit. I only wish that
more garbage collectors were available as
libraries.

Anyways, my favorite language has been Ruby
for many years, that's partially because
I like how easy it is to script from C,
for some efficiency where I need it. So
Potion is my little attempt to make a
language that feels easy, but compiles
to machine code, to give me the speed of
(un-optimized) C code and to use all the
C function call conventions so that I
can link to other C libs. I want to see
if its still possible to encroach on C.

I should say that O'Caml and Haskell
already do a fine job as C competitors,
so it's not like anything here is that
new.


## ~ lua's influence ~

Potion uses a register-based bytecode VM that
is nearly a word-for-word copy of Lua's. The
code is all different, but the bytecode is
nearly identical. And Lua's 5.1 bytecode design is
heavily inspired by the 1997 Inferno OS "Dis VM".
<http://doc.cat-v.org/inferno/4th_edition/dis_VM_specification>
which was Lucent's faster and smaller competitor
of Sun's stack-based and GC-heavy JVM.
Dis/Limbo/Inferno concepts only got a recent boost
with Google adopting it as natively compiled Go.

I also spelunked Neko VM for some ideas and
found this comparison very useful.
<http://web.archive.org/web/20150204201136/nekovm.org/lua>

I also took from the Lua the notion of using
tables for everything. And, like Lua, lists
and tables are interchangeable. But, in Potion,
I kept their representations separate.

I also use immutable strings, like Lua.

However, my object representation is more
like Neko or Ruby, where objects are all
represented by a word (32-bit on x86 and
64-bit on x86-64).


## ~ the parts of potion ~

1. the parser
   - written in peg
     (see `core/syntax.g`)
   - produces a syntax tree
   - the tree is a Potion object that can
     be traversed in the code
   - see `potion_source_load()`
     in `core/compile.c`

2. the compiler
   - compiles a syntax tree into
     bytecode (see `core/compile.c`
     or `./potion -c -V code.pn`)
   - has its own file format
     (save to `.pnb` files)
   - interception of keywords
     happens here, rather than in
     the parser, to simplify parsing

3. the bytecode vm
   - executes bytecode
   - used mostly to ensure correctness
   - helpful in debugging
   - run code on non jitted vm with performance
     penalty

4. the jit vm
   - compiles bytecode into a function
     pointer
   - uses the same argument passing strategy
     as c functions

5. the garbage collector
   - generational, copying, near exact
   - marks the stack, then uses a cheney
     loop to catch everything else
   - stack scanning is automatic (both
     the jit and the bytecode vm keep
     their registers on the call stack)
   - liberal use of volatile for object
     pointers, to allow them to move
     after any allocation
   - based on a collector called qish


## ~ the jit ~

potions jits intel x86 32+64-bit and ppc.
arm jit is in preparation.
ppc callcc/yield is buggy.
The X86 JIT is in `core/vm-x86.c`.
The jit translates Potion bytecode into
32-bit or 64-bit machine code.

In the code:

    add = (x, y): x + y.

The `add` closure is compiled into a c function
pointer looking like this:

    PN (*)(Potion *P, PN cl, PN self, PN x, PN y)

This means you can actually load this closure
into C and call it directly.

    Potion *P = potion_create();
    PN add = potion_eval(P, "(x, y): x + y.");
    PN_F addfn = PN_CLOSURE_F(add);
    PN num = addfn(P, add, 0, PN_NUM(3), PN_NUM(5));
    printf("3 + 5 = %d\n", PN_INT(num));

The macros are there to allow bytecode to be
wrapped in a function pointer, if needed. The
inside story looks like this:

    PN num = ((struct PNClosure *)add)->method(
               P, add, 0, PN_NUM(3), PN_NUM(5));

Note that calling from C into such methods directly
does not fill optional parameter values, it does not
check the parameter count and types.
If you need to use closures with optional parameters
and need to leave them out in the call, you need
to use the bytecode method.

Wrong:

    PN add = potion_eval(P, "(x=N|y=N): x + y.");
    PN_F addfn = PN_CLOSURE_F(add);
    PN num = addfn(P, add, 0, PN_NUM(3), PN_NUM(5));
    printf("3 + 5 = %d\n", PN_INT(num));     //ok
    //PN num1 = addfn(P, add, 0, PN_NUM(3)); //wrong num1

Better:

    long oldflags = (long)P->flags;
    if (P->flags & EXEC_JIT) P->flags -= EXEC_JIT;
    PN add = potion_eval(P, "(x=N|y=N): x + y.");
    PN_F addfn = PN_CLOSURE_F(add);
    PN num = addfn(P, add, 0, PN_NUM(3), PN_NUM(5));
    printf("3 + 5 = %d\n", PN_INT(num));     //ok
    PN num1 = addfn(P, add, 0, PN_NUM(3));   //ok
    P->flags = (Potion_Flags)oldflags;

## ~ the jit's assembly ~

Now, let's talk about the compiled code.

Earlier the example was,

    add = (x, y): x + y.

So we have a function with two arguments
and a math operation.

The C equivalent is,

    PN add(Potion *P, PN cl, PN self, PN x, PN y) {
      PN reg1 = x;
      PN reg2 = y;
      reg1 = PN_NUM(PN_INT(reg1) + PN_INT(reg2));
      return reg1;
    }

The arguments are moved to registers on the
stack. Then converted to ints. Then added.
And, lastly, the result is cast as a Potion
object.

The machine code produced by the JIT will
be very close to the code produced by a
compiler with the optimizations turned off.
(_At least for the present this is true._)

Like C functions, Potion keeps local variables
on the stack. The call stack is like a big tape
measure that goes on and on. Since this function
has two registers (reg1 and reg2,) we need
two inches from the tape measure.

<http://en.wikipedia.org/wiki/Call_stack>
(See the parts that say "Local data storage"
and "Argument passing".)

Our machine code says _"give me two word-sized slots in the stack"_
as the first order of business.

We store `x` and `y` in those two slots. (See
the comment about C argument passing in the
next section.)

Then we do the math. Even the `PN_INT` and
`PN_NUM` macros are fundamentally math.

If you run potion with `-V` or `--verbose` on this script,
you'll see a bytecode disassembly.

    ; function ; 16 bytes
    ; (x, y) 2 registers
    .local "x" ; 0
    .local "y" ; 1
    [1] getlocal 0 0
    [2] getlocal 1 1
    [3] add 0 1
    [4] return 0

Each of these lines corresponds to a line of
C code inside that function a few paragraphs
back. You see how they line up there?


## ~ x86 and x86-64 ~

64-bit CPU instructions are a superset of
32-bit CPU instructions. And many 32-bit
instructions can be converted to 64-bit by
prefixing them.

Take this code,

    8b 55 f8  // mov %ebp[-1] %edx
    8b 45 fc  // mov %ebp[-2] %eax
    83 e8 04  // sub 0x4 %edx
    89 50 04  // mov %edx %eax[1]

These are the 32-bit instructions for altering
an upval. (_A variable in upper scope_.) Since
a closure doesn't have access to the registers
of another function, these variables are passed
as pointers (the `PNWeakRef` struct.)

This code loads the upval into `%eax` and the new
value into `%edx`. To get the upval's actual struct
pointer, we subtract 4 from `%edx`. Finally, we
move the new value into offset 4 in the struct
pointed to by the upval.

In 64-bit code this would be,

    48 8b 55 f0  // mov %rbp[-1] %rdx
    48 8b 45 f8  // mov %rbp[-2] %rax
    83 e8 04     // sub 0x4 %edx
    48 89 50 08  // mov %rdx %rax[1]

All it took was adding the `0x48` prefix to our
mov instructions and changing our offsets to
measure 8 bytes rather than 4. The third
instruction actually stays the same, since
we're only doing math on the lower 32-bits of
the upval.

We now call the registers `%rax` and `%rdx`.
That's just what the CPU folks call `em`.

The only other technique that changes between
32-bit and 64-bit is function calling. On 32-
bit, we keep all the function arguments on
the stack. On 64-bit, some function arguments
are passed through registers.

That's the purpose behind the function named
`potion_x86_c_arg`. To shuttle the arguments
in and out, depending on the architecture.


## ~ using disassemblers ~

I don't really know the x86 instruction set
too well and most often I just write something
in C and disassemble it.

    $ gcc test.c -o test
    $ objdump --disassemble-all test

However, `objdump` doesn't come with the OS X
dev tools, so you can use the included
`tools/machodis.pl` by Robert Kelp to inspect the
binary.

    $ ./tools/machodis.pl test

If you compile with `make DEBUG=1` and `config.mak`
finds any of the following disassembly libraries *udis86*,
*libdistorm64* or *libdisam*, they are used automatically.
See `core/vm-dis.c` and `config.mak`.


## ~ the garbage collector ~

Potion's garbage collector is based on the
work Basile Starynkevitch performed with his
small generational GC entitled *Qish*. As with
everything else in Potion, the goal isn't to
be the best, but to experiment with what sort
of advanced features can be squeezed into a
language while keeping the code compact.

Since Potion interpreters are per-thread,
each interpreter has its own GC heap. This
allows interpreters to run in parallel and
to start up and shut down independently.
Perhaps one day the GC will run in parallel.
 
Qish is a two-finger generational collector.
(Also called a _Cheney loop_.) This collector
only has two generations. When the time for
garbage collection arrives, everything from
the call stack is scanned and moved from the
young gen to the older gen. The insides of
all those objects are then scanned and moved
as well. The goal is simply to wipe out the
birth region and replace it with another.

What about objects from the birth region
that are stored off in some old hashtable
somewhere? Or stuff added to a global object?

The birth region has a write barrier: an
array of pointers to any object which has
recently changed. If a new object is added
to a tuple, it's the tuple's job to notify
GC so that the tuple will get scanned on
the next GC pass.

What happens when the old generation runs
out of room? A new old generation is
allocated and we do a major collection:
the call stack is scanned and everything from
the birth region AND the full-to-the-brim
older region are both scanned and moved on
to the new generation.

This all has some obvious pitfalls. First,
that's a lot of memory moving around all
the time -- pointers moving around. And,
second, that's a lot of scanning to do:
does the entire heap have to be scanned
every time there's a major collection?

Yes, it's a lot of work and I hope to get
some real benchmarks and determine a new
strategy to buy some speed: either through
introducing a third generation or by
defragmenting memory or by allowing pages
to be partially unmapped. Probably the last
one: I'll weight sections of the older
generation and then clear out those sections
as they become more sparse.

As for moving pointers around, that's
why the volatile keyword is a part of the
PN typedef.

Does Potion perform *exact GC*? Not strictly.
During the stack scanning phase, Potion has
no idea what pointers are legit. For now,
this is resolved by investigating the header
of each pointer's object for a valid type
object. The type can then be compared with
the contents of the object to be sure the
object is real. To me, this feels exact
enough. (If, in practice, it proves to
be too messy, I may resort to using a
macro to identify exact pointers, just as
Qish does.)


## ~ the volatile keyword ~

Since objects move freely about, Potion
requires use of the *volatile* keyword for
all access to object ids (`PN`) from C.
As most people don't readily understand
the volatile keyword, let's go over it.

There are three common uses of the volatile
keyword in Potion code:

    PN string = potion_byte_str("...");

Since `volatile` is part of the PN typedef
this code actually means:

    volatile unsigned long string =
      potion_byte_str("...");

In this case, the `volatile` keyword will
generally prevent the PN from being stored
in a system register when the code is
optimized. Even for caching purposes.

So any time the `string` variable is used,
it'll be looked up directly on the stack.
This means the code won't be optimized fully,
but it also means the value will be looked
up every time we use it, in case GC has changed
the object's place on us.

    struct PNObject * volatile string = ...;

If we cast the string to a `PNObject` pointer,
we need to be sure to place the `volatile`
keyword after the star. This tells the compiler
that the _pointer_ is the volatile one, not the
struct itself.

    struct PNUserType {
      PN_OBJECT_HEADER
      PN a, b, c;
    };

In any structs which are wrapped by
a Potion object, you need to be sure that any
object handles are marked volatile as well.
In the above case, the PN typedef handles that.

Also be sure to not nest function calls which return PN
objects. With an optimizing (gcc/clang -O3) such
intermediate results might be stored in a register, as
volatile only applies to lvalues and the GC will not see this
PN and move references to it away.
Use temp. PN variables instead.

This is a bit tedious to keep in mind, but
is compensated for by Potion's automatic stack
scanning (a technique which has brought easy
C extensions to Ruby) and object convention.
If your object pointers are all 8-byte aligned,
you won't need to write a GC marking function.

## ~ the fixed memory section ~

Potion does have one other section of
memory: a permanent generation containing
all of the objects that are used to bootstrap
Potion's environment. The Lobby object, the
class lookup table, the GC structures, etc.

This section of memory is positioned at the
beginning of the first birth area. After the
birth area, fills that memory is detached
(like the saucer section of the Enterprise)
and lives its life independantly of the other
regions, not to be moved or released until
the Potion interpreter is destroyed.


## ~ object overhead ~

Primitives (such as numbers, booleans and nil) are
immediate values in Potion, kept in a single-word stack
slot.

Everything else is given a spot in memory. Every
allocated object has a single word header specifying
its type. Here's a breakdown of what each object looks
like on 32-bit:

    PNObject = 4 + (N * 4) bytes
      4 bytes (type = 0x250009)
      N * 4 bytes (each object field)
  
    PNWeakRef = 8 bytes
      4 bytes (type = 0x250004)
      4 bytes (pointer to object)
  
    PNString = 13 + N bytes
      4 bytes (type = 0x250003)
      4 bytes (length of string)
      4 bytes (an incremental id used as hash)
      N + 1 bytes (UTF-8 + '\0')
  
    PNBytes = 9 + N bytes
      4 bytes (type = 0x25000c)
      4 bytes (length of string)
      N + 1 bytes (string + '\0')
  
    PNTuple = 8 + (N * 4) bytes
      4 bytes (type = 0x250006)
      4 bytes (length of tuple)
      N * 4 bytes (each tuple item)
  
    PNClosure = 16 + (N * 4) bytes
      4 bytes (type = 0x25005)
      4 bytes (function pointer)
      4 bytes (signature pointer)
      4 bytes (length of attached data)
      N * 4 bytes (attached data)
  
    PNData = 8 + N bytes
      4 bytes (type = 0x250011)
      4 bytes (length of contained data)
      N bytes (data)

The smallest allocation unit in Potion is
16 bytes. Everything larger than 16 bytes
is aligned to 8 bytes. This is true for both
32-bit and 64-bit platforms.

The reason for this figure is to allow room for
the `PNFwd` struct:

    unsigned int  // POTION_FWD or POTION_COPIED
    unsigned int  // allocated space in this slot
    unsigned long // pointer to moved object

Any time an object is moved by GC or reallocated,
a temporary `PNFwd` is placed in the memory slot to
redirect callers temporarily. In practice, you
shouldn't ever need to deal with these values,
since the stack scanning phase will rewrite them
automatically.

The first type of `PNFwd` is `POTION_FWD`. This type
is used for reallocation. Potion only has four
built-in types that are subject to reallocation:
tuples, tables, byte strings, and internal buffers.
(Internal buffers are used by the JIT to store
machine code output and by the object model to
keep an array of type classes in circulation.)

The `POTION_FWD` pointers will happen between GC
cycles, so they stay in place for awhile. The
API around those four built-ins is designed to
check for forwarded pointers whenever they're
passed in as an object. One the next GC cycle
hits all the pointers will be updated to use
the new objects (even if `POTION_FWD` occurs in
the old generation, due to the write barrier.)

The other `PNFwd` type is a `POTION_COPIED` pointer.
This is used by the GC to denote an object which
has moved to the old generation. These should
not appear outside the GC cycle. The `PNFwd` is
placed immediately when the object is moved and
is then used to rewrite pointers found on the
stack or inside other objects. After the GC
cycle, these pointers will disappear when the
its outdated generation is discarded.

## ~ debugging internals ~

Build with `DEBUG=1` (the default `make`): `-g`, `-DDEBUG`, the `DBG_*` tracing macros and the
`-D` flags of the command line are only there then. `DEBUG=0` gives the `-O3 -DNDEBUG` build.
`make ASAN=1` adds the address sanitizer.

### command line

    p2 -V -B -e 'my $x = 3; print $x'   # the AST (potion only) and the bytecode, run in the VM
    p2 -J file.pl                       # the same program in the x86 JIT
    p2 -Dp file.pl                      # trace the parser rules (-DP: with their text)
    p2 -DG file.pl                      # trace the garbage collector
    p2 -D? file.pl                      # list the debugging flags: i t c p P J G v
    p2 --compile file.pl                # file.tc, the dumped bytecode; `p2 file.tc` runs it
    p2 --stats                          # sizes of the object structs and the GC state

`-B` and `-J` select the bytecode VM or the JIT: a crash that only happens in one of them is a
bug in the VM or the code generator (`core/vm.c`, `core/vm-x86.c`), one that happens in both is in
the compiler, the library or the garbage collector.

### a type, a value and a stack trace in gdb

All values are `PN`, one machine word. A small integer has its low bit set, `nil` is `0`, true
and false are `6` and `2`, anything else is a pointer to an object whose first word is the
type id (`struct PNObject`: `vt`, then the random `uniq`). The type ids are
`0x250000 + n` (`core/potion.h`): `0x250008` is a tuple, `0x250005` a string, `0x250007` a
closure, `0x25000d` an AST node (`PNSource`), `0x25000f` a proto, `0x250011` a table.

    $ gdb --args bin/p2 -B -e 'my @a = (1, 2); print "@a\n"'
    (gdb) break potion_tuple_push
    (gdb) run
    Breakpoint 1, potion_tuple_push (P=0x7ffff7dde088, tuple=140737351944096, value=140737351942880) at core/table.c:283
    (gdb) bt 4
    #0  potion_tuple_push (...) at core/table.c:283
    #1  yy_3_arg (G=0x410020, yytext=0x4140c0 "S", ...) at syn/syntax-p5.c:3444
    #2  yyDone (G=0x410020) at syn/syntax-p5.c:2904
    #3  p5_code_parse_from (...) at syn/syntax-p5.c:29080
    (gdb) p potion_type(tuple)       # the type id: 2424840 == 0x250008, a tuple
    (gdb) p ((struct PNTuple *)tuple)->len
    (gdb) p ((struct PNTuple *)tuple)->set[0]
    (gdb) p potion_type(value)       # 2424837 == 0x250005, a string
    (gdb) p ((struct PNBytes *)value)->chars    # the bytes of a string
    (gdb) p/x ((struct PNObject *)tuple)->vt    # the same without calling code

Useful breakpoints: `potion_vm` (a function starts: `proto` is its `struct PNProto`),
`potion_gc_alloc`/`potion_garbagecollect` (allocation and collection), `potion_jit_proto` (a proto
gets compiled to machine code), `potion_error`/`potion_fatal` (an error is raised), and the
parser actions `yy_*_NAME` for a rule `NAME` of the grammar `syn/syntax-p5.y`.

Do not trust a `PN` you copied around: the collector moves objects, so a pointer into a
string or a code buffer that was valid before an allocation is stale after it. Print the
object again from the variable that the stack scan updates, not from a saved address. In a DEBUG
build the collector keeps an object start map (see `pngc_sane_object` in `core/gc.c`); a stale
word on the stack that is not the start of an object is ignored instead of being copied.
`catch signal SIGSEGV` and `bt` at the crash show which collector phase (`potion_mark_minor`,
`potion_mark_major`, `potion_gc_copy`) walked into the bad object.

### from the AST to the VM

The parsers (`syn/syntax.y` for potion, `syn/syntax-p5.y`, `syn/syntax-p6.y`) are greg PEG
grammars. They build a tree of `PNSource` nodes (`core/ast.c`, `enum PN_AST`: `AST_CODE`,
`AST_VALUE`, `AST_ASSIGN`, `AST_MSG`, `AST_EXPR`, `AST_LIST`, `AST_BLOCK`, ...), each with up to
three kids `a[0..2]` and the line. `core/compile.c` (`potion_source_asmb`) walks it once and
emits the register bytecode of `core/opcodes.h`; `-V` prints both. A potion example:

    $ bin/potion -V -e 'x = (a=1, b=2) x'
    code (assign (expr (msg ("x")) expr (list (assign (expr (msg ("a")) expr (value (1))),
          assign (expr (msg ("b")) expr (value (2)))), msg ("x"))))
    .local x ; 0
    [ 1] newtuple 0 0
    [ 2] loadk    1 0   ; a
    [ 3] loadpn   2 3   ; 1
    [ 4] settable 0 2
    ...

The shapes that are not obvious:

- An `expr` is not an expression but a message chain: its kids are a receiver followed by the
  messages sent to the result of the one before. `a.b.c(1)` is `(expr (msg a) msg b msg c (list 1))`.
  A message without a list is a variable or method reference, with a list it is a call
  (`self`, `loadk name`, `bind`, `call`: look the method up, then call the closure).
- `(list (assign ...), (assign ...))` is a table literal when every item is an assignment
  (`newtuple` plus `settable` for each pair), a tuple when the items are plain values.
  This is the `(table (expr))` the issue means: `{ k => v }` in p5 is a `list` of `assign`
  nodes sent the message `table`; `[ 1, 2 ]` a `list` sent `clone`.
- An `assign` to a name is `setlocal` when the name is a local of the function, `setupval`
  when an enclosing function owns it, else it defines a method on the lobby (the next section).
  An assign to an `expr` with several kids is a store through the chain: `callset` fetches the
  setter of the last message, then it is called with the key and the value.
- A function is a `proto` (a closure template with its own bytecode, locals, upvals and values).
  `.upval` names are variables of an enclosing function; the VM boxes the captured variable
  (`PNWeakRef`) when the closure is created, both functions see the box.
- Operators are their own nodes (`AST_PLUS`, `AST_EQ`, ...) compiled to one op (`add`, `eq`); on
  a value that is not a number the VM sends the message (`+`, `cmp`) to the left operand.
  The p5 grammar rewrites perl's operators into other nodes and messages (`$a . $b` is `p5join`,
  `$$r = 1` is `p5assign`, `eq` compares the `p5str` strings): the parser output is what you
  should look at, not the perl source.

### control keywords are not keywords

`if`, `elsif`, `else`, `while`, `loop`, `return`, `break`, `continue` and `class` are ordinary
`msg` nodes whose name is one of the interned symbols `PN_if`, `PN_while`, ... (`extern PN` in
`core/potion.h`). The grammar only has to parse `name list block`; `core/compile.c` compares the
message name (`PN_S(t,0) == PN_if`) before it compiles a send and turns it into `testjmp`/`notjmp`
and `jmp` instead of a call, `break` and `continue` into jumps patched to the end or the test of
the enclosing loop (`struct PNLoop`, with the loop labels of p5). No lexer has to know them and they
need no grammar rule per construct; a language grammar that
wants perl's statement modifiers or `unless` rewrites them to these messages (`p5_while_mod`,
`p5_cfor`, `p5_forlist` in `syn/syntax-p5.y` build `while` nodes for `for`, `foreach` and `until`).

### how globals are read

There is no global variable table. A global is a method of the lobby, the root of the
vtable chain that every object inherits from: `potion_define_global(P, name, value)` sends the
lobby the message `def`, and a read of a name that is neither a local nor an upvalue is the
send of that name to `self`:

    [ 1] self     1
    [ 2] loadk    0 0   ; p5lprint
    [ 3] bind     0 1
    [ 4] call     0 2

`bind` looks the name up in the vtable of the receiver and its parents (`potion_bind`,
`core/objmodel.c`), so a lobby method is visible everywhere, builtin functions and user globals
alike. The p5 grammar keeps perl's file level variables as locals of the main function instead
(`my`, `our` and undeclared variables), subs see them as upvals (`-V` of `$g = 5; sub f { $g }`
shows `.upval $g`); only the interpreter's own names (`$_`, `$a`, `$b`, the match variables) and
the natives of `lib/p5` are lobby methods. The p5 packages (`lib/p5/p5oo.c`) register subs in a
table by their qualified name instead.

### how mixins work

An object has no class: its type id selects a `vtable` (`P->vts`), a hash of methods with a
parent. A mixin is such a vtable: `potion_type_new(P, type, parent)` creates one, `potion_method`
or the `def` message adds a method to it, and the same call on a builtin type extends it for
everything of that type (`lib/p5/p5string.c` adds `ref`, `p5joins` and friends to the string,
tuple and table vtables; `potion_class` creates a new type with instance variables). Lookup is
`potion_bind`: the vtable of the receiver, then its parents, up to the lobby. An object that is
a table stores per-object methods as table entries, so "swap the mixin for the span of a
closure" is a change of which vtable the receiver's type id names, which is cheap for the
inline method cache and expensive for everything that cached the old binding (the JIT keeps a
per-vtable `mcache`, `potion_vtable_mcache`, invalidated by `potion_lookup_overridden`).
The perl 5.38 `class` feature of the p5 grammar is built on `potion_class`, the p5 packages
(`bless`, `@ISA`) are not: they dispatch by name in `p5meth` and keep the class of a blessed
reference in a registry keyed by its `uniq`.

### the threading model that should be added

Not done, only prepared: `struct Potion_State` ("one per thread, houses its own garbage
collector") is the interpreter, and every API takes `Potion *P`, but there is one global `P`,
and the allocator, the lobby and the string table are shared by construction. A thread model
that fits the existing code:

- one `Potion_State` per OS thread, each with its own `PNMemory`: a private birth region (the
  allocation fast path stays lock free, `potion_gc_alloc`) and a minor collection that stops
  only that thread;
- the old region becomes common to all threads (the TODO in `struct PNMemory`), guarded by one
  lock for promotion and major collections, which stop all threads at their next allocation;
- no object is shared between birth regions: values cross threads by being copied (tuples,
  strings, tables, closures by value) or by a `PNWeakRef` to a mutex protected old object, as
  `lib/aio.c` already does for the libuv loop, which is single threaded and woken from other
  threads with `uv_async_send`;
- the lobby and the vtables are frozen after startup or copied on write, so method lookup and
  the JIT's method caches need no lock;
- the conservative scan of the C stack (`potion_mark_stack`) then has to run per thread on its
  own stack range, which is why callcc/continuations (`core/callcc.c`) must stay per thread.

`doc/concurrency.md` collects the language level ideas (automatic threads, isolation, parallel
loops) that this runtime part would have to carry.
