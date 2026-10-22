#!/bin/sh
# usage: test/runtests.sh [-q] [-pn|-p2|-p5] [testfile...]
# In -p2 mode test/**/*.p2 files are run besides *.pl; if cperl is installed
# (CPERL env var, default `cperl`; CPERL= disables) of version 5.42 or newer each
# *.p2 file is also run with it in the first p2 pass and compared against
# the same '#=>' expectation. So is perl itself (PERL env var, default `perl`)
# if it is 5.42 or newer, the p2 language level (use v5.42; with the class
# feature), e.g. PERL=$HOME/perl5/perlbrew/perls/perl-5.44.0/bin/perl. Without PERL the
# newest such perl in $PERLBREW_ROOT (default ~/perl5/perlbrew) is used.
# A '# p2-only' line in a file opts out.
#        cmd="valgrind -q bin/potion" test/runtests.sh

cmd=${cmd:-bin/potion}
cmd2=${cmd2:-bin/p2}
ECHO=/bin/echo
SED=sed
EXPR=expr
EXT=pn
MAKE=${MAKE:-make}
maxpass=6

# An ASAN build (config.inc): the fake stacks of detect_stack_use_after_return hide
# the locals from the conservative GC, and the x86 JIT code crashes under ASAN,
# so only the VM and compiler passes run.
ASANBUILD=0
if grep -q -- '-fsanitize=address' config.inc 2>/dev/null; then
  ASANBUILD=1
  export ASAN_OPTIONS="${ASAN_OPTIONS:+$ASAN_OPTIONS:}detect_stack_use_after_return=0"
fi

# make -s $cmd
count=0; failed=0; pass=0
cmdi="$cmd -I"; cmdx="$cmdi -X"
cmdc="$cmd -c"; extc=b
QUIET=
if [ "x$1" = "x-q" ]; then QUIET=1; shift; fi

verbose() {
  if [ "x$QUIET" = "x" ]; then ${ECHO} $@; else ${ECHO} -n .; fi
}

if [ "x$1" = "x-pn" ]; then shift; maxpass=3; fi
if [ "x$1" = "x-p2" ]; then
    shift
    maxpass=3
    cmd=${cmd2}; EXT=pl
    cmdi="$cmd --inspect"; cmdx="$cmdi -J"
    cmdc="$cmd --compile"; extc=c
fi
# p5 marker tests (test/p5/*.t without a .expected file): same runner as
# -p2 but over the .t extension, so '#=> expected' comments in the file
# are compared against full stdout (run_p5_test.sh's .expected convention
# is still used by the older test/p5/*.t files).
# If perl is installed (PERL env var, default `perl`; PERL= disables), each
# file is also run with it (-Mfeature=say) in the first (VM) pass and compared
# against the same '#=>' expectation, to cross-check p2 against perl. A file
# that only p2 can run (p2-only syntax) can opt out with a '# p2-only' line.
if [ "x$1" = "x-p5" ]; then
    shift
    PERL=${PERL-perl}
    if [ -n "$PERL" ] && ! command -v "$PERL" >/dev/null 2>&1; then PERL=; fi
    maxpass=3
    cmd=${cmd2}; EXT=t
    # no --inspect: print/say return true like perl, the result is not part of the output
    cmdi="$cmd"; cmdx="$cmdi -J"
    cmdc="$cmd --compile"; extc=c
fi

CPERL=${CPERL-cperl}
P2PERL=${PERL-perl}
# no usable PERL given: the newest perl >= 5.42 of a perlbrew installation
if [ -z "${PERL+x}" ] && ! "$P2PERL" -e 'exit($] >= 5.042 ? 0 : 1)' >/dev/null 2>&1; then
  for pp in `ls -d ${PERLBREW_ROOT:-$HOME/perl5/perlbrew}/perls/perl-5.*/bin/perl 2>/dev/null | sort -t. -k2,2nr -k3,3nr`; do
    if "$pp" -e 'exit($] >= 5.042 ? 0 : 1)' >/dev/null 2>&1; then P2PERL=$pp; break; fi
  done
fi
if [ -n "$P2PERL" ] && ! "$P2PERL" -e 'exit($] >= 5.042 ? 0 : 1)' >/dev/null 2>&1; then P2PERL=; fi
# only a cperl at the p2 language level (5.42, `use v5.42;`) can run the *.p2 tests
if [ -n "$CPERL" ] && ! "$CPERL" -e 'exit($] >= 5.042 ? 0 : 1)' >/dev/null 2>&1; then CPERL=; fi

test -f $cmd || ${MAKE} -s $cmd
test -f $cmd2 || ${MAKE} -s $cmd2

# linux/bsd only
old_LIBRARY_PATH="$LD_LIBRARY_PATH"
export LD_LIBRARY_PATH="lib:$LD_LIBRARY_PATH"
# windows finds the dlls (libp2, a module's dependencies) along PATH
case `uname -s` in
  MINGW*|MSYS*|CYGWIN*) export PATH="$PWD/lib:$PATH" ;;
esac

verbose() {
  if [ "x$QUIET" = "x" ]; then ${ECHO} $@; else ${ECHO} -n .; fi
}

if test -z $1; then
    ${MAKE} -s bin/potion-test bin/p2-test bin/gc-test
    ${ECHO} running potion API tests
    bin/potion-test
    if [ $EXT = pl ]; then
      ${ECHO} running p2 API tests
      bin/p2-test
    fi
    ${ECHO} running GC tests
    bin/gc-test
fi

while [ $pass -lt $maxpass ]; do 
    ${ECHO}; 
    if [ $pass -eq 0 ]; then 
	t=0; 
	whattests="$cmd VM tests"
    elif [ $pass -eq 1 ]; then 
        t=1; 
	whattests="$cmd compiler tests"
    elif [ $pass -eq 2 ]; then 
        t=2; 
	whattests="$cmd JIT tests"
	jit=`$cmd -v | sed "/jit=1/!d"`; 
	if [ $ASANBUILD = 1 ]; then jit=; fi
	if [ "$jit" = "" ]; then 
	    ${ECHO} "no JIT, skipping the JIT tests"
	    if [ $maxpass -le 3 ]; then pass=`expr $pass + 1`; continue; fi
	    pass=`expr $pass + 1`
            cmd=${cmd2}; t=0; EXT=pl
	    cmdi="$cmd --inspect"; cmdx="$cmdi -J";
	    cmdc="$cmd --compile"; extc=c
	    whattests="$cmd VM tests"
	fi;
    elif [ $pass -eq 3 ]; then 
        cmd=${cmd2}; t=0; EXT=pl
	cmdi="$cmd --inspect"; cmdx="$cmdi -J";
	cmdc="$cmd --compile"; extc=c
	whattests="$cmd VM tests"
    elif [ $pass -eq 4 ]; then 
        t=1; 
	whattests="$cmd compiler tests"
    elif [ $pass -eq 5 ]; then 
        t=2;
	whattests="$cmd JIT tests"
	jit=`$cmd -v | sed "/jit=1/!d"`
	if [ $ASANBUILD = 1 ]; then jit=; fi
	if [ "$jit" = "" ]; then 
	    pass=`expr $pass + 1`
	    break
	fi
    fi

    if test -n "$1" && test -f "$1"; then
	what="$*"
	if [ "${what%.pl}" = "$what" -a "${what%.p2}" = "$what" -a $EXT = pl -a $pass -ge 3 ]; then
	    ${ECHO} skipping p2
	    pass=6
	    break
	fi
	if [ "${what%.pn}" = "$what" -a $EXT = pn -a $pass -le 3 ]; then
	    ${ECHO} skipping potion
	    pass=3
            cmd=${cmd2}; t=0; EXT=pl
	    cmdi="$cmd --inspect"; cmdx="$cmdi -J";
	    cmdc="$cmd --compile"; extc=c
	    whattests="$cmd VM tests"
	fi
    else
        if $(grep "SANDBOX = 1" config.inc >/dev/null); then
	    what=`ls test/**/*.$EXT|grep -Ev "test/misc/(buffile|load)\.$EXT"`
        else
	    what=test/**/*.$EXT
        fi
	# *.p2 tests only run in the p2 passes
	if [ $EXT = pl ]; then what="$what `ls test/**/*.p2 2>/dev/null`"; fi
    fi

    ${ECHO} running $whattests

    for f in $what; do 
	case $f in */p6/*|*/test.pl) continue ;; esac
	case `uname -s` in MINGW*|MSYS*|CYGWIN*) case $f in */cmdline-np.t|*/inplace.t) continue ;; esac ;; esac # need a unix shell for -e
	look=`cat $f | sed "/\#=>/!d; s/.*\#=> //"`
	# *.p2 tests print for themselves (like p5 tests): no --inspect
	if [ "${f%.p2}" != "$f" ]; then
	    cmdi="$cmd"; cmdx="$cmdi -J"
	elif [ $EXT = pl ]; then
	    cmdi="$cmd --inspect"; cmdx="$cmdi -J"
	fi
	#echo look=$look
	if [ $t -eq 0 ]; then
	    verbose $cmdi -B $f
	    # Command substitution strips trailing newlines itself. Normalize
	    # CRLF output without GNU sed's non-portable \n pattern.
	    for=`$cmdi -B "$f" | tr -d '\015'`
	elif [ $t -eq 1 ]; then
	    $cmdc "$f" > /dev/null
	    fb=$f$extc
	    verbose $cmdi -B $fb
	    for=`$cmdi -B "$fb" | tr -d '\015'`
	    rm -rf "$fb"
	else
	    verbose $cmdx $f
	    for=`$cmdx "$f" | tr -d '\015'`
	fi;
	if [ "$look" != "$for" ] && [ "$for" = "not connected" ] && grep -q "requires a TCP daemon on localhost port 80" "$f"; then
	    ${ECHO}
	    ${ECHO} "skipping $f: no TCP daemon on localhost port 80"
	elif [ "$look" != "$for" ] && { [ "$for" = "no event" ] || [ -z "$for" ]; } && grep -q "requires a working libuv" "$f"; then
	    ${ECHO}
	    ${ECHO} "skipping $f: no working libuv/aio on this platform"
	elif [ "$look" != "$for" ]; then
	    ${ECHO}
	    ${ECHO} "** $f: expected <$look>, but got <$for>"
	    failed=`expr $failed + 1`
	elif [ $t -eq 0 ] && [ -n "$CPERL" ] && [ "${f%.p2}" != "$f" ] && ! grep -q '^# p2-only' "$f"; then
	    pfor=`$CPERL "$f" 2>/dev/null | tr -d '\015'`
	    if [ "$look" != "$pfor" ]; then
		${ECHO}
		${ECHO} "** $f: expected <$look>, but cperl got <$pfor>"
		failed=`expr $failed + 1`
	    fi
	elif [ $t -eq 0 ] && [ -n "$P2PERL" ] && [ "${f%.p2}" != "$f" ] && ! grep -q '^# p2-only' "$f"; then
	    pfor=`$P2PERL "$f" 2>/dev/null | tr -d '\015'`
	    if [ "$look" != "$pfor" ]; then
		${ECHO}
		${ECHO} "** $f: expected <$look>, but perl got <$pfor>"
		failed=`expr $failed + 1`
	    fi
	elif [ $t -eq 0 ] && [ -n "$PERL" ] && ! grep -q '^# p2-only' "$f"; then
	    pfor=`$PERL -Mfeature=say "$f" 2>/dev/null | tr -d '\015'`
	    if [ "$look" != "$pfor" ]; then
		${ECHO}
		${ECHO} "** $f: expected <$look>, but perl got <$pfor>"
		failed=`expr $failed + 1`
	    fi
	fi
	count=`expr $count + 1`
    done
    pass=`expr $pass + 1`
done

${ECHO}
if [ $failed -gt 0 ]; then
    ${ECHO} "$failed FAILS ($count tests)"
    exit 1
else
    ${ECHO} "OK ($count tests)"
fi

#if [ -z "`grep DDEBUG config.inc`" ]; then
#    ${ECHO} run examples
#    log=log.example
#    git log -1 >> $log
#    for f in example/*.pn; do
#        ${ECHO} time bin/potion $f | tee -a $log
#        time bin/potion $f 2>&1 | tee -a $log
#    done
#    for f in example/*.p[2l]; do
#        ${ECHO} time bin/p2 $f | tee -a $log
#        time bin/p2 $f 2>&1 | tee -a $log
#    done
#fi

export LD_LIBRARY_PATH="$old_LIBRARY_PATH"
