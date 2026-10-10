# ポーション

# A Short Pamphlet

#### by why the lucky stiff

Well, here we go. You want to learn Potion[^1]? Sure, okay. But first, bear in mind that Potion isn’t done yet. And it does very little. It’s completely esoteric. So long as we’ve got that straight.

So why exactly are you learning this nascent do-nothing probably-broken language? For fun, right? I’m giving you till the end of sentence to come up with a good reason.

This pamphlet probably isn’t for beginners to computer programming. Just for those curious about what Potion is like.

## An Understanding

Potion’s mantra is: *Everything is an object. But objects aren’t everything.*

Etched beneath that in faint pencil: *Oh, and everything is a function.*

## Special Things?

So is there anything unusual about Potion? Anything of particular interest?

- Potion compiles programs down to machine code.
- It includes a small generational near-exact garbage collector,  
  spending on avg 4ms on medium sized heaps.
- It is two languages: one for code, one for data.
- It’s written in under 10,000 lines of C.

Potion is inspired by fellow languages Io, Ruby, OCaml, Lua, REBOL and C. In that order. (Also influenced by the works of Ian Piumarta, Nicolas Cannasse and Basile Starynkevitch.)

## A Sip

Let’s start with some code.

### Ad Nauseam

    <code>
      loop: 'quaff' print.
    </code>

I know this isn’t terribly useful, but it’s an infinite printing of the string `'quaff'`.

A colon starts a **code block**. And the period ends it. The `loop` command then runs the code block endlessly. You will see the colon and period combination reused throughout Potion.

The `print` message is sent to the string `'quaff'`. Strings are an object, like everything. They receive messages. Messages are separated from objects by a space. (In most languages, you use a dot to separate messages. But, like English, the period signifies the end of something rather than a separation.)

### A List

    <code>
      ('cheese', 'bread', 'mayo') at (1) print
    </code>

This one prints the message ‘bread’. The stuff in parentheses is a **list**. We have a list of foodstuffs. And it’s being sent a message named `at`. Every list has an `at` message that looks up an item by its position in the list.

Notice that after the `at` message is another list. The `1` is an argument to `at`. It’s the position we want to look up. It looks like a list (and it *is* a list,) but we call it an argument because it comes after a message.

### A Table

    <code>
      (language='Potion', pointless=true) at (key='language') print
    </code>

Okay, this one looks similar to the list, but it’s not. Here we have a **table**. The table pairs up things. The string `'language'` is paired up with the string `'Potion'`.

Notice the arguments are also a table. Lists and tables are sort of interchangeable. You can use a table or a list as arguments.

## The Functional Side

Functions are throughout Potion. Whether it be anonymous lambdas, blocks or type functions.

### A Function

    <code>
      minus = (x, y): x - y.
      minus (y=10, x=6)
    </code>

This one illustrates a bit better how tables get used as argument lists. We have the `minus` variable which contains a **function**. The function subtracts `y` from `x`. In this case, it’ll return `-4`.

(This is similar to keyword arguments in Lua and Python, yes. However, it’s important to see that lists and tables and arguments in Potion all share the same syntax. Less to remember.)

### A List as a Function

    <code>
      foods = ('cheese', 'bread', 'mayo')
      foods (2)
    </code>

Here’s a case where a list is being called as a function. Yes, everything is a function! We could also have called: `foods (index=2)`.

Strings, tables, numbers are also functions. The following returns the 3rd character of the string.

    <code>
      "ヘ(^_^ヘ)(ノ^_^)ノ" (2)
    </code>

Even functions are functions! I invented this concept. Just like Steve Jobs will one day.

### A Block

    <code>
      (dog='canine', cat='feline', fox='vulpine') each (key, val):
        (key, ' is a ', val) join print.
    </code>

Functions can also be attached to methods, for use as anonymous blocks (as in Ruby.)

These blocks are merely the last argument. This also works: `each ((key, val): key print.)`.

## The Object-Oriented Side

    <code>
      Person = class: /name, /age, /sex.
      Person print = ():
        ('My name is ', /name, '.') join print.
    </code>

The above describes a **class** in Potion. Objects are very memory-efficient. Each Person object will store three properties: the name, age and sex. (These are not kept in a hashtable. They are kept in memory, immediately following the object’s header.)

Properties use a slash before their name. This comes from the computer filesystem, where slashes are used before the names of folders in which to store files.

However, if you are desperate to use an object as a hashtable, you can store anything you like in an object’s method table. Not just methods can go there. Anything can be wrapped in a closure.

### Making Objects

    <code>
      p = Person ()
      p /name string print
    </code>

Yep, classes are functions, too! They create objects.

In this case, the name of `p` hasn’t been set, so the code will print `nil`.

But how does subclassing work?

### A Subclass

    <code>
      Policeman = Person class (rank): /rank = rank.
      Policeman print = ():
        ('My name is ', /name, ' and I''m a ', /rank, '.') join print.

      Policeman ('Constable') print
    </code>

The `class` message just gets sent to the parent class. And that’s it.

A Policeman now has four properties: `/name`, `/age`, `/sex` and `/rank`.

Notice the first line of the code. The end of the statement is a block. That block is the object’s constructor. So, in the last line, we’re passing in the string ‘Constable’ as the `rank` argument.

## Licks

    <code>
      app = [window (width=200, height=400)
              [para 'Welcome.', button 'OK']]
      app first name
    </code>

Lastly, here we have a **lick**. This is the data language brought up earlier in the section about Special Things. This code will print `'window'` since that’s the name of the first item in the lick.

Two languages in one? What for? I mean you can do anything you want from code, right?

There can be problems with expressing data in code. Say the above example was written in Potion code. Some thing like this:

    <code>
      app = window(width=200, height=400):
        para 'Welcome.'
        button 'OK'.
    </code>

In order to get this to work, you need methods for `window`, `para` and `button`. This could clutter the namespace. Also, under what context are those messages available? Are they methods of the created window? Or do they go through some kind of proxy object? We don’t know what’s going on behind this code. (Which isn’t bad at all, if it works.)

By having a separate little data language, you can build tree structures of arbitrary elements (akin to HTML) which act as a kind of common structure between Potion libraries. (You can also think of it as code which has been parsed, but not executed.)

### The Flexibility Of…

Licks generally follow the look of Potion. Strings can be quoted. Tables are curved on the edges.

    <code>
      [name (attr1='string', attr2=10) 'TEXT HERE']
    </code>

Every lick can have a name, a table of attributes, and a list of children. The list of children can, instead, be a Potion data type, such as a number or string or something. (No, this isn’t a new idea. It’s very much like E4X[^2], but without XML.)

However, licks also allow unquoted strings, to give you some added flexibility.

    <code>
      math = Grammar [
        digit  <- n:[0-9] { n number }
        value  <- d:digit+ | '(' e:expr ')' { d or e }
        expr   <- l:value op:[*/] r:value
        {
          if (op == '*'): l * r. else: l / r.
        }
        main   <- expr
      ]
    </code>

Here we have four entries in a lick, getting passed to the `Grammar` method. The entries are: `digit`, `value`, `expr` and `main`. (So, for example, the `digit` entry will be paired with the string `"<- n:[0-9] { n number }"`.

The lick syntax keeps track of nested groups of parentheses, square brackets and curly braces. The unquoted mode merely needs to start with a letter, number or non-token character.

Be careful of commas in unquoted strings. This won’t work:

    <code>
      [dollars $10,000]
    </code>

You’ll end up with two entries in the lick: a `dollars` entry and a `000` entry.

Commas are okay inside of nested groups, though. So, this would be okay:

    <code>
      [dollars $(10,000)]
    </code>

## Pause For Effect

Okay, let’s stop. So you’ve basically seen everything in the language already. Sorry about that. It kind of blows that there’s no surprises left. :( But, hey, I said it’s little, right?

Are you starting to see some patterns in this code?

- Methods, blocks and functions all use the colon-dot syntax.
- Tables and lists are reused as function and block arguments. And as attributes in licks.
- Generally, lowercase is used. Except in the case of class names. (But it’s nothing special, you can use lowercase for classes if you want.)

## Potion Syntax

Now that you have a feel for what Potion can do, let’s talk about every one of Potion’s tokens in detail.

### Encoding

Potion source code is always in UTF-8. Likewise, all Potion strings are UTF-8. Potion is too small to include other encodings in its core API.

### Lines

Potion code lines are separate by a newline. (Or a CR-LF works as well.)

    <code>
      x = 1
      y = 2
    </code>

Throughout Potion, a comma is equivalent to a newline.

    <code>
      x = 1, y = 2
    </code>

This also means that tables can be written using newlines as separators:

    <code>
      (language='Potion'
       pointless=true)
    </code>

### Spaces

Spaces are used to separate messages and objects and operators, but they usually are only used for clarity’s sake.

To borrow an earlier example, it turns out the following is legit.

    <code>
      ('cheese','bread','mayo')at(1)print
    </code>

There is some flexibility, in order to avoid senseless syntax errors.

### Comments

Lines preceded by the octothorpe are ignored by Potion.

    <code>
      # this foul business...
      String length = (): 10.
    </code>

### Code Blocks

Lines can be grouped together into blocks. Think of it as a collection of code you’ve group together to run later. Blocks start with a colon and end with a dot.

    <code>
      block = :
        'potion' print.
    </code>

## Built-in Types

Potion has a rather small set of built-in types and libraries. This is to keep the language core small for folks who want to embed Potion and to constraint its memory footprint.

### True, False, Nil.

Potion has three keywords for these built-in types.

`nil` indicates that a variable is empty: it has no value and no type. (This isn’t exactly true, though. Its class is `NilKind`.)

`true` and `false` are boolean values belonging to the `Boolean` class.

Anything which is not `nil` or `false` is considered a positive value. (Which means that the number zero and empty strings are considered `true` in if statements.)

### Numbers

Basic numbers take up no memory and are passed as plain (32-bit or  
64-bit, depending on your processor) integers, but we loose one bit of  
precision for a tag bit. Arithmetic methods accept the type Number,  
which is Integer or Double and converts them as it fits.

    <code>
      5
      47
      -25
      0xFF
    </code>

Double numbers use a boxed double-precision floating point number.

    <code>
      999.9
      2.00001
      2e+2
      2.4e-8
    </code>

Seemless arbitrary-precision math is planned for the 1.0 release.

### Strings

Strings begin with a single quote, followed by a series of UTF-8 characters, then a final single quote. There is only one escape code: `''` for an embedded single quote.

    <code>
      'Cornelius'
      'Tuesday
      Jun 29th, 2009'
      'C:\Program Files\Potion'
    </code>

Double-quoted strings allow a number of escape codes.

\* `"\n"` for a newline.  
\* `"\r"` for a carriage return.  
\* `"\t"` for a tab.  
\* `"\uXXXX"` for a Unicode character.

## Expressions

In my opinion, a language can be called “functional” if every statement and expression returns a value. Even `if` statements or class definitions. This is just how Potion is designed. Every statement is a function, since it takes arguments and returns results. There is no `void`.

### if, elsif, else

The `if` keyword tests its arguments for truth. Its block is then executed if they pass.

    <code>
      if (age > 100): 'ancient'.
    </code>

Like any function, `if` returns a result. If the condition fails, you get `nil`. Otherwise, you get the result of the code executed inside the block.

Beyond `if`, you can chain `elsif` and `else` to catch other conditions.

    <code>
      author =
        if (title == 'Jonathan Strange & Mr. Norrell'):
          'Susanna Clarke'.
        elsif (title == 'The Star Diaries'):
          'Stanislaw Lem'.
        elsif (title == 'The Slynx'):
          'Tatyana Tolstaya'.
        else:
          '... probably Philip K. Dick'.
    </code>

These three keywords can all be executed like functions, even though they are compiled into basic instructions.

### loop

The `loop` keyword executes its block endlessly.

    <code>
      loop: 'quaff' print.
    </code>

### while

The `while` keyword will execute its block endlessly, as long as its arguments stay true.

    <code>
      count = 8
      while (count > 0):
        'quaff' print
        count--.
    </code>

### to

Potion has no `for` keyword. One alternative is the `to` keyword.

    <code>
      1 to 5 (a):
        a string print.

      num = argv(1) number
      1 to (num, (a):
        a print.)
    </code>

The `to` keyword will loop from the first number, up to the last.

Another option is the `times` method, which starts at zero.

    <code>
      5 times: 'Odelay' print.
    </code>

Notice how the block doesn’t need parentheses if we don’t want the block arguments coming in.

### return

Returns a value from the function. You only need to use the `return` keyword explicitly if you’re looking to quit in the middle of the function.

## Names and Objects

The most expressive parts of a program are the naming and creation of unique objects.

### Variables

Generally, any UTF-8 set of characters which isn’t seen as a built-in type or operator can be used as a variable. Variables are assigned with a plain equals sign.

    <code>
      t = true
      $$ = [dollars 100]
      HTTP = 'Hypertext Transfer Protocol'
      わが身 = self 
    </code>

You must set a variable before using it (even if just to `nil`.) Otherwise, Potion sees a message, sent to `self`.

### Messages

Messages follow the same rules as variables. A message name can be any UTF-8 character which isn’t a built-in type or operator.

    <code>
      Number $ = ():
        [dollars (amount=self)].
      100 $
    </code>

In this example, the `$` message converts the number into a kind of currency lick.

### Queries

Objects can be asked if they respond to a message, by prefixing the message with a question mark.

    <code>
      if (3 ?gender):
        "Huh? Numbers are sexed? That's amazing." print.
    </code>

You can also optionally execute the message by send it arguments. And you can space out the quiz mark, if you want.

    <code>
      3 ? gender ()
    </code>

Since numbers don’t have a `gender` method, this’ll give `nil` rather than an error.

A better example of this would be if you are in a web application and you wanted to see if the query string contained a `session` entry.

    <code>
      HomePage get = (url):
        session = url query ? at ('session').
    </code>

That way if `query` is empty, you won’t get an error, you’ll just get nil. Assuming that `query` is a table, though — you’ll get back the value filed under the `'session'` key.

### Paths

A path is any set of non-whitespace UTF-8 characters preceded by a slash. A path is also called an “instance variable” in programming jargon.

    <code>
      BTree = class: /left, /right.
      b = BTree ()
      b /left = BTree ()
      b /right = BTree ()
    </code>

Paths cannot be randomly added to the object after the object is created. Each object has a strict set of paths. Every path which is used in the constructor is added to the object upon creation.

### Path Queries

As with methods, you can query an object to see if it has a given path.

    <code>
      BTree = class: /left, /right.
      b = BTree ()

      if (b ? /left):
        'left path found!' print.
    </code>

## And That’s It?

Yes, that’s the important stuff for now. But a number of new features are coming to Potion soon[^3].

- Assignment extended to match and bind, for expressing conditions on object layout.
- FFI (`extern`), debuggers (-d), external compilers (-c) (\_to c and native executable for the start\_).
- Types and symbol tables. Types to be faster, and symtabs to be slower and more like a LISP and perl/ruby.
- Compiler macros and compiler optimizations, like tailcall, constant folding, strength reduction.
- Bignums, marshalling.
- More libraries for os, ui, db, …
- And also: Native threading. This is a biggie. It requires lock-free bags and some new native  
  atomic insns and parallelizing loops. The work-stealing sister language sol[^4] looks good,  
  it can yield to other threads. But if or if not to steal work, and if or not share data.
- Channels and blocking recv for typed task communications al la Go. On top of `aio`.
- Eventually lists, implemented via licks, but with a LISPy feeling.

This are already in, but documented elsewhere[^5]:

- Mixins, a syntax for blending methods into classes.
- MOP, changing metaclasses, the behavior of classes.
- Coroutines, a technique for inprocess threading.
- Continuations, for saving and resuming the stack, `here` and `yield`.
- Exceptions for graceful error handling.
- Asynchronous non-blocking IO in the `aio` module.
- Buffered stream FILE\* IO in the `buffile` module.
- `readline` module providing a little repl if called without script
- Optional, named and default signature arguments.

Thankyou for reading along. I hope this was only a brief interruption to an otherwise lush and exotic life. I’m sorry to disappoint with so little. Perhaps one day I can make things right?

## Footnotes

## Potion’s and p2 license

p2 is free software, released under an MIT and ARTISTIC 2 license —  
the very brief paragraphs below. There is satisfaction simply in  
having created this. Please use this how you may, even in commercial  
or academic software. I’ve had a good time and am want for nothing.

<hr />

potion is copyright © 2009 why the lucky stiff, MIT licensed.

p2 and maintainance code by perl11 http://perl11.org/p2/  
© 2013 perl11 org, licensed under the ARTISTIC 2.

HOWEVER. The follow MIT licensed codes have also been employed:

Be it known, parts of the object model taken from [obj.c](http://www.piumarta.com/software/id-objmodel/).  
© 2007 Ian Piumarta

And, also, the design of the VM bytecode is from [Lua](http://luaforge.net/docman/view.php/83/98/ANoFrillsIntroToLua51VMInstructions.pdf).  
© 1994-2006 Lua.org, PUC-Rio

The Mersenne Twister ([MT19937](http://www.math.sci.hiroshima-u.ac.jp/~m-mat/MT/MT2002/emt19937ar.html))  
© 1997-2002, Makoto Matsumoto and Takuji Nishimura

Lastly, [khash.h](http://attractivechaos.awardspace.com/khash.h.html).  
© 2008, by Attractive Chaos

Permission is hereby granted, free of charge, to any person obtaining  
a copy of this software and associated documentation files (the  
“Software”), to deal in the Software without restriction, including  
without limitation the rights to use, copy, modify, merge, publish,  
distribute, sublicense, and/or sell copies of the Software, and to  
permit persons to whom the Software is furnished to do so, subject to  
the following conditions:

The above copyright notice and this permission notice shall be  
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND,  
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF  
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND  
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY  
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE  
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

[^1]: Potion the [programming language](http://github.com/perl11/potion). Not the drink of fables.

[^2]: [ECMAScript for XML](http://en.wikipedia.org/wiki/ECMAScript_for_XML). (See also: [s-expression](http://en.wikipedia.org/wiki/S-Expression.))

[^3]: [Potion wiki](https://github.com/perl11/potion/wiki/_pages).

[^4]: [sol](http://github.com/rsms/sol).

[^5]: [Potion Documentation](http://perl11.org/potion/html/).
