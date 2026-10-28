# The interactive loop and the debugger

`potion` and `p2` have two interactive modes that share one set of `:` commands:

- the **loop**: a read-eval-print loop on a terminal, with line editing and history (readline module),
- the **debugger**: a script is run line by line, with breakpoints and frame inspection.

Both need a terminal. The line editor keeps its history in `history.txt` in the current directory.

## Starting them

| command               | mode     | language of the lines |
|-----------------------|----------|-----------------------|
| `potion`              | loop     | potion                |
| `potion -d`           | loop     | potion                |
| `potion -d file.pn`   | debugger | potion                |
| `p2`                  | usage, no loop |                 |
| `p2 -d`               | loop     | perl5 (p5)            |
| `p2 -d file.pl`       | debugger | perl5, or p6 for `.p6`/`.raku` |

The subtle difference: **`potion` without a program starts the loop by itself, `p2` only with `-d`**,
as `perl -d` is the debugger entry in perl. `p2` without arguments prints its usage.
A program that is piped in (`p2 < file`, `p2 -`) is a program, not a loop. Use a file to debug it.

`-d` on a program turns on the instrumented VM (no JIT) and stops before every line.
Inside the loops `-d` is switched off again after startup: the loop *is* the interactive
front end, and the debugger would otherwise read the next input line as its own command.

## The loop

Type a line, get `=> value`. `q` or `^D` leave. The first line printed tells the commands.

| | `potion` | `p2 -d` |
|-|----------|---------|
| language | potion, evaluated with `eval` | perl5, evaluated like `eval STRING` |
| result | `=> value` | `=> value`, `=> undef` for undef |
| error | the error object is printed (`Syntax error before text ...`) | the text of `$@` is printed (`boom`, `Internal parser error: ...`) |
| `h` | not a command (the help is printed at start) | prints the help |
| after an error | the loop goes on | the loop goes on |
| variables and subs | gone with the line (one program per line) | kept between lines, see below |
| input | one line | continues on `.. ` lines while a bracket or quote is open, an empty line ends it |

Variables persist in the `p2 -d` loop (they do **not** in the `potion` loop: every line is compiled
and run as its own program there). Each p5 line is rewritten before it is evaluated:

- a scalar `$x` lives in a session table, so an assignment is kept at once (also when the line dies)
  and a closure made on one line sees later values of `$x`;
- an array or hash the line mentions is loaded before the line and stored after it (also after a `die`);
- `my $x = 1` at the start of a top level statement declares the session variable; a `my` inside a
  block or `for my $i` is a real lexical of that line (what a closure captures);
- named subs (`sub sq { $_[0]*$_[0] }`, also over several lines) are defined again in front of every line,
  after the session arrays and hashes they use are loaded, so a sub sees and changes them; a sub is
  only kept when its input compiled;
- `local $x`, `local @a`, `local $h{k}` of a session variable save the variable before the input and
  restore it after it (also after a `die`);
- `$x` is text inside '...', and interpolates inside "..." and `@{[ ]}`; slices (`@a[1,2]`, `@h{'a','b'}`,
  `%h{...}`) work and assign;
- an input with an unclosed `(`, `[`, `{` or quote continues on the next line (prompt `.. `); an empty
  line ends it.

Limits: `local` restores at the end of the input, not at the end of its block, and saves the whole
array or hash for an element; a closure captures session arrays by value; `$a`/`$b` stay plain; a heredoc or a regex with an unbalanced bracket or a
lone quote continues the input (end it with an empty line).

## `:` commands (loop and debugger)

| command | meaning |
|---------|---------|
| `:subs` | the named subs, sorted |
| `:classes` | the classes |
| `:methods CLASS` | the methods of CLASS |
| `:b N`, `:break N` | set a breakpoint at line N |
| `:B N` | delete the breakpoint at line N |
| `:bl` | list the breakpoints |

A line starting with `:` that is not a command prints `unknown command`.

The listings show what is defined **so far**. A perl `sub` or `package` is registered when its
definition runs, so in a script that is stopped at its first line `:subs` and `:methods` are
still empty; continue (`:c`, `:n`) past the definitions first.

What the listing commands show differs between the two languages, because they look
into different registries:

- **`:subs`**: in `p2` the registered perl subs with their package, `main::hello`, `Dog::speak`.
  In `potion` the closures that are defined as methods of the lobby (the toplevel object); a function
  assigned in a loop line, `hello = (): 1.`, is a local of that line and is not listed.
- **`:classes`**: in `p2` the perl packages (`Animal`, `Dog`, `main`) and the runtime types
  that are not built in. In `potion` the classes made with `class`. Both also list the
  internal `Regex` and `Syntax` types. Metaclasses (`<metaclass: X>`) are hidden.
- **`:methods CLASS`**: in `potion` the methods of the class (`speak`). In `p2` the subs with
  the prefix `CLASS::` (`Dog::speak`), without those of the parents in `@ISA`.
  Both are printed when a name is both.

Breakpoints are by line number only (not per file), and are remembered for the whole
process. In the loops they can be set, but nothing runs under them: they apply to a
program that is debugged in the same process.

## The debugger commands

When a script runs under `-d`, it stops before each line and prints `(file:line):	source`
followed by the prompt `> `.

| command | meaning |
|---------|---------|
| `<enter>`, `:s`, `:step` | go to the next line, also into called functions |
| `:n`, `:next` | go to the next line of this function, running the calls in between |
| `:c`, `:cont` | continue until a breakpoint (or the end of the program) |
| `:b N`, `:break N`, `:B N`, `:bl` | breakpoints, as above |
| `:q` | leave the debugger and run the program to the end |
| `:exit` | leave the debugger and exit |
| `:h` | the help |
| `:subs`, `:classes`, `:methods CLASS` | as in the loop |
| `/l` | the locals of the stopped function, `$y = 5` |
| `/u` | the upvals, the variables captured from an outer function |
| `/r` | the registers of the frame |
| `/v` | the constants (values) of the function |
| `/p` | the paths (instance variables, `/x`) of the function |
| any other line | evaluated as code of the language (potion, or perl5 for `p2`) and printed |

`:n` steps over calls by comparing the register depth of the frame, so it also skips the
lines of a recursive call to the same function. A breakpoint inside a skipped call still stops.

### Differences between potion and p2

- **First stop.** `potion` stops first at the synthetic `load "debug"` / `debug()` statements
  that start the debugger module, then at the lines of the script. `p2` stops at the
  statements of the script; subs that are defined before the main program are hoisted, so
  the first stop is often a later line.
- **Line numbers.** The reported line is where the statement was parsed. It can be off by one or
  two for multi line statements, for closing braces, and for hoisted subs.
- **One line subs.** A sub written on one line, `sub f { ... }`, gets no stop of its own:
  `:s` stays in the caller, and `/l` shows the caller's locals. A sub that spans lines
  stops on its lines, and `/l`, `/u` show its own variables.
- **Variables.** The names in `/l` follow the language: `$x`, `@a`, `%h`, `$_` for p5,
  plain names for potion. A p5 sub signature may show `$y:=710564442`: that number is the
  internal marker for an argument that was not passed.
- **Evaluation at the prompt.** The expression is compiled as a separate program. It does
  not see the lexical variables of the stopped function yet; use `/l` and `/u`.
- **Compiled programs.** Programs compiled with `--compile=c` or `--compile=exe` run without
  the VM debugger. Debug the source.

## Examples

```
$ p2 -d
p2 -d: q or ^D quits, h shows this help, any other line is evaluated as perl5, :subs ...
>> 1+2
=> 3
>> die "boom\n"
boom
>> q
```

```
$ p2 -d script.pl
(script.pl:6):	my $a = f();
> :break 8
breakpoint at line 8
> :n
> :cont
a=2
breakpoint at line 8
(script.pl:8):	my $b = 2;
> /l
l0 $n = 10
...
```

## Implementation

The `:` commands are one C function, `potion_debug_cmd` in `core/vm.c`. The debugger itself is
`potion_debug` in the same file, called by the VM for every `OP_DEBUG` instruction (the
compiler emits one per new source line). `/r /l /u /v /p` read the frame layout of the VM,
`upvals | locals | self | registers`. The loops are in `front/potion.c` and `front/p2.c`;
the p5 registries are exported as `p5subnames` and `p5pkgnames` (`lib/p5/p5oo.c`).
