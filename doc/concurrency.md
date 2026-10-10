# Concurrency ideas

## threads should be created and scheduled automatically

The compiler and scheduler needs to decide if a block warrants a new thread or not.

## Non-deterministic parallelism

- undefined order of evaluation of arguments  
  function arguments are evaluated in parallel before the function is called. (as in a LISP let block)

<!-- -->

    <code>
        sub sample (a,b,c) {}
        sample (x+1, subcall(), y-z);
    </code>

- explicit parallel loops and blocks  
  Idea from [DPJ](http://dpj.cs.uiuc.edu/DPJ/)  
  See e.g. [VikramAdve_2011-Jul-CAV-DPJOverview](http://www.cs.utah.edu/events/conferences/cav2011/content/presentations/VikramAdve_2011-Jul-CAV-DPJOverview.pdf)

<!-- -->

    <code>
        parallel for i (0..10) {<stmts>*}  Parallel loop
        parallel { <stmts>+ }              Parallel statements
    </code>

## Isolation

    <code>
        atomic { <stmts>+ }
    </code>

## Samples

    <code>
        parallel {
          atomix x = 0;
          atomix x = 1; //Error
        }

        parallel {
          parallel { atomic x = z; atomic y = z; }
          atomic z = 1; //Error
        }

        //1st line at parallel: writes Rx, Ry, reads Rz
        //1st line inside parallel: writes atomix Rx, atomicx Ry, reads atomic Rz
    </code>

cilk: [spawn,sync](http://supertech.csail.mit.edu/cilk/)

    <code>
        sub fib(n) {
          return n if n<2;
          x = spawn fib(n-1);
          y = spawn fib(n-2);
          # sync;   # sync filled in automatically before each return
          return (x+y);
        }
        result = spawn fib(ARGV);
        sync;
        print result;
    </code>

c#: [async,await](http://msdn.microsoft.com/en-us/library/vstudio/hh191443.aspx)

potion: here,yield,coro

[DPC++](http://gppd.inf.ufrgs.br/projects/mcluster/dpc++/)

[DPJ](http://dpj.cs.uiuc.edu/DPJ/)
