# posix (linux, bsd, osx, solaris) + mingw with gcc/clang only
.SUFFIXES: .y .c .i .i2 .o .obj .opic .o2 .obj2 .opic2 .html
.PHONY: bench.summary all default bins libs pn p2 static usage config clean doc rebuild check test test.pn test.p2 test.p6 test.p5 \
	examples bench tarball dist release install grammar doxygen website \
	testable spectest_checkout spectest_init spectest_update
.NOTPARALLEL: test test.pn test.p2 test.p6 test.p5

SRC = core/asm.c core/ast.c core/compile.c core/compile-c.c core/contrib.c core/gc.c core/internal.c core/lick.c core/mt19937ar.c core/number.c core/objmodel.c core/primitive.c core/regex.c core/string.c core/syntaxobj.c core/table.c core/unicode.c core/vm.c
PLIBS = readline buffile aio
PLIBS_SRC = lib/aio.c lib/buffile.c lib/readline/readline.c lib/readline/linenoise.c
GREGCFLAGS = -O3 -DNDEBUG

# bootstrap config.inc with make -f config.mak
include config.inc
# ASAN builds: the fake stacks of detect_stack_use_after_return hide the locals from the
# conservative GC, so the test scripts (runtests.sh, roast5.sh, ..) must run without them
ifneq (,$(findstring -fsanitize=address,${DEBUGFLAGS}))
export ASAN_OPTIONS := $(if ${ASAN_OPTIONS},${ASAN_OPTIONS}:)detect_stack_use_after_return=0
endif
PCRE2_DIR = 3rd/pcre/src
PCRE2_CONFIG = ${PCRE2_DIR}/pcre2.h
PCRE2_INTERNAL_CONFIG = ${PCRE2_DIR}/config.h
PCRE2_CHARTABLES = ${PCRE2_DIR}/pcre2_chartables.c
PCRE2_LIB = 3rd/pcre/.libs/libpcre2-8.a
PCRE2_MODULES = auto_possess chkdint chartables compile compile_cgroup \
	compile_class config context convert dfa_match error extuni find_bracket \
	jit_compile maketables match match_data match_next newline ord2utf \
	pattern_info script_run serialize string_utils study substitute substring \
	tables ucd valid_utf xclass
PCRE2_SRC = $(addprefix ${PCRE2_DIR}/pcre2_,$(addsuffix .c,${PCRE2_MODULES}))
ifneq (${WIN32},1)
OBJEXT = o
else
OBJEXT = obj
endif
PCRE2_OBJ = ${PCRE2_SRC:.c=.p2.${OBJEXT}}
PCRE2_HEADERS = $(wildcard ${PCRE2_DIR}/*.h)
INCS += -I${PCRE2_DIR}
LIBS += ${PCRE2_LIB}

# arbitrary precision integers: libtommath, all of it, with 28/60 bit digits (16 bit ones on win64, where long is 32 bit)
SRC += core/bigint.c $(wildcard 3rd/libtommath/bn_*.c)
INCS += -I3rd/libtommath
ifeq (${WIN32},1)
INCS += -DMP_16BIT
endif

ifneq (${DISABLE_CALLCC},1)
SRC += core/callcc.c
endif
ifneq (${SANDBOX},1)
SRC += core/file.c core/load.c
else
PLIBS = readline aio
PLIBS_SRC = lib/aio.c lib/readline/readline.c lib/readline/linenoise.c
ifeq ($(WIN32),1)
PLIBS_SRC += lib/readline/win32fixes.c
endif
endif

ifeq (${JIT_X86},1)
SRC += core/vm-x86.c
else
ifeq (${JIT_PPC},1)
SRC += core/vm-ppc.c
endif
ifeq (${JIT_ARM},1)
SRC += core/vm-arm.c
endif
endif

SRC_SYN    = syn/syntax.c
SRC_P2_SYN = syn/syntax-p5.c
SRC_POTION = front/potion.c
SRC_P2     = front/p2.c

FPIC =
OPIC = o
ifneq (${WIN32},1)
  ifneq (${CYGWIN},1)
    FPIC = -fPIC
    OPIC = opic
  else
    ifneq (${SANDBOX},1)
      PLIBS = readline buffile
    else
      PLIBS = readline
    endif
  endif
else
  # one object set: mingw code is always position independent
  FPIC = -fPIC
  OPIC = obj
  CFLAGS += ${FPIC}
endif
OBJ = ${SRC:.c=.${OBJEXT}}
OBJ2 = ${SRC:.c=.${OBJEXT}2}
OBJ_SYN = ${SRC_SYN:.c=.${OBJEXT}}
OBJ_POTION = ${SRC_POTION:.c=.${OBJEXT}}
OBJ_P2 = ${SRC_P2:.c=.${OPIC}2}
OBJ_P2_SYN = ${SRC_P2_SYN:.c=.${OBJEXT}2}
PIC_OBJ = ${SRC:.c=.${OPIC}}
PIC_OBJ_SYN = ${SRC_SYN:.c=.${OPIC}}
PIC_OBJ_POTION = ${SRC_POTION:.c=.${OPIC}}
PIC_OBJ_P2 = ${SRC_P2:.c=.${OPIC}2}
PIC_OBJ_P2_SYN = ${SRC_P2_SYN:.c=.${OPIC}2}
OBJ_TEST = test/api/potion-test.${OBJEXT} test/api/CuTest.${OBJEXT}
OBJ_P2_TEST = test/api/p2-test.${OBJEXT} test/api/CuTest.${OBJEXT}
OBJ_GC_TEST = test/api/gc-test.${OBJEXT} test/api/CuTest.${OBJEXT}
OBJ_GC_BENCH = test/api/gc-bench.${OBJEXT}
BINS = bin/potion${EXE} bin/p2${EXE}
PNLIB = $(foreach l,potion p2,lib/lib$l${DLL})
PNLIB += $(foreach s,syntax syntax-p5 syntax-p6,lib/p2/lib$s${DLL})
#EXTLIBS = $(foreach m,uv pcre,lib/p2/lib$m${LOADEXT})
#EXTLIBS = -L3rd/pcre -lpcre -L3rd/libuv -luv
EXTLIBS = -Llib -luv
PNLIB = lib/libpotion${DLL}
EXTLIBS = -Llib -luv -lpthread
ifeq (${WIN32},1)
#LIBUV = lib/libuv-1.dll lib/libuv.dll.a
#EXTLIBS += /usr/i686-w64-mingw32/lib/libws2_32.a
LIBUV = lib/libuv-11.dll lib/libuv.dll.a
EXTLIBS += -lws2_32
else
LIBUV = lib/libuv${DLL}
endif
ifeq (${APPLE},1)
LIBUV_SRC = 3rd/libuv/src/fs-poll.c 3rd/libuv/src/inet.c \
	3rd/libuv/src/threadpool.c 3rd/libuv/src/uv-common.c \
	3rd/libuv/src/version.c 3rd/libuv/src/unix/async.c \
	3rd/libuv/src/unix/core.c 3rd/libuv/src/unix/dl.c \
	3rd/libuv/src/unix/fs.c 3rd/libuv/src/unix/getaddrinfo.c \
	3rd/libuv/src/unix/getnameinfo.c 3rd/libuv/src/unix/loop-watcher.c \
	3rd/libuv/src/unix/loop.c 3rd/libuv/src/unix/pipe.c \
	3rd/libuv/src/unix/poll.c 3rd/libuv/src/unix/process.c \
	3rd/libuv/src/unix/signal.c 3rd/libuv/src/unix/stream.c \
	3rd/libuv/src/unix/tcp.c 3rd/libuv/src/unix/thread.c \
	3rd/libuv/src/unix/timer.c 3rd/libuv/src/unix/tty.c \
	3rd/libuv/src/unix/udp.c 3rd/libuv/src/unix/darwin.c \
	3rd/libuv/src/unix/darwin-proctitle.c 3rd/libuv/src/unix/fsevents.c \
	3rd/libuv/src/unix/kqueue.c 3rd/libuv/src/unix/proctitle.c
LIBUV_OBJ = ${LIBUV_SRC:.c=.p2.${OBJEXT}}
LIBUV_HEADERS = $(wildcard 3rd/libuv/include/*.h 3rd/libuv/src/*.h 3rd/libuv/src/unix/*.h)
endif
EXTLIBDEPS = ${LIBUV}
DYNLIBS = $(foreach m,${PLIBS},lib/potion/$m${LOADEXT}) lib/p2/aio${LOADEXT} lib/p2/libsyntax-p6${LOADEXT} lib/p2/libp6${LOADEXT} lib/p2/libp5${LOADEXT}
PLIBS_OBJ = ${PLIBS_SRC:.c=.${OPIC}}
PLIBS_OBJS = ${PLIBS_SRC:.c=.${OBJEXT}}
PLIBS_OBJS2 = ${PLIBS_SRC:.c=.${OBJEXT}2}
P5_SRC = lib/p5/p5.c lib/p5/p5file.c lib/p5/p5table.c lib/p5/p5string.c lib/p5/p5eval.c lib/p5/p5number.c lib/p5/p5ref.c lib/p5/p5oo.c lib/p5/p5glob.c lib/p5/p5class.c lib/p5/p5tie.c
P5_OBJS2 = ${P5_SRC:.c=.${OBJEXT}2}
OBJS = ${OBJEXT} ${OBJEXT}2
ifneq (${OPIC},${OBJEXT})
  OBJS += ${OPIC} ${OPIC}2
endif
# libtommath's platform RNG uses MP_HAS() feature tests and requires dead-code
# elimination so unavailable OS backends do not remain as unresolved calls.
$(foreach o,${OBJS},3rd/libtommath/bn_s_mp_rand_platform.${o}): CFLAGS += -O1
DOC = doc/p2.1 doc/potion.1 doc/start.md doc/p2-extensions.md doc/glossary.md doc/design-decisions.md \
  doc/concurrency.md doc/debugger.md
DOCHTML = $(patsubst %.md,%.html,$(filter %.md,${DOC}))
# parallel recipes move the same archives: a mv that lost the race must not fail the build
LIBPNA_AWAY = if [ -f lib/libpotion.a ]; then mv lib/libpotion.a lib/libpotion.a.tmp 2>/dev/null || true; fi
LIBPNA_BACK = if [ -f lib/libpotion.a.tmp ]; then mv lib/libpotion.a.tmp lib/libpotion.a 2>/dev/null || true; fi
LIBP2A_AWAY = if [ -f lib/libp2.a ]; then mv lib/libp2.a lib/libp2.a.tmp 2>/dev/null || true; fi
LIBP2A_BACK = if [ -f lib/libp2.a.tmp ]; then mv lib/libp2.a.tmp lib/libp2.a 2>/dev/null || true; fi

CAT  = /bin/cat
ECHO ?= /bin/echo
MV   = /bin/mv
SED  = sed
EXPR = expr
GREG = bin/greg${EXE}
AR     ?= ar
RANLIB ?= ranlib
# a mingw target built on a non-windows host cannot run its own greg.exe
ifeq (${WIN32},1)
  ifeq (,$(findstring MINGW,$(shell uname -s))$(findstring MSYS,$(shell uname -s)))
    CROSS = 1
  endif
endif
HOSTCC ?= cc
ifeq (${CROSS},1)
  GREGCROSS = syn/greg
else
  GREGCROSS = ${GREG}
endif

# perl11.com only
WEBSITE = ../perl11.com

default: pn p2 libs
	+$(MAKE) -s usage

all: default libs static docall test
ifneq (${SANDBOX},1)
pn: bin/potion${EXE} ${PNLIB}
p2: bin/p2${EXE} ${PNLIB}
else
pn: static
p2: static
endif
bins: ${BINS}
libs: ${PNLIB} ${DYNLIBS}
static: lib/libpotion.a bin/potion-s${EXE} lib/libp2.a bin/p2-s${EXE}
rebuild: clean default test

usage:
	@${ECHO} " "
	@${ECHO} " ~ using p2 ~ (same as perl)"
	@${ECHO} " "
	@${ECHO} " Running a script or code."
	@${ECHO} " "
	@${ECHO} "   $$ p2 example/fib.pl"
	@${ECHO} "   $$ p2 -e \"code\""
	@${ECHO} " "
	@${ECHO} " Dump the AST and bytecode inspection for a script. "
	@${ECHO} " "
	@${ECHO} "   $$ p2 --verbose example/fib.pl"
	@${ECHO} " "
	@${ECHO} " Compiling to bytecode."
	@${ECHO} " "
	@${ECHO} "   $$ p2 --compile example/fib.pl"
	@${ECHO} "   $$ p2 example/fib.plc"
	@${ECHO} " "
	@${ECHO} " Potion builds its JIT compiler by default, but"
	@${ECHO} " you can use the bytecode VM by running scripts"
	@${ECHO} " with the -B or --bytecode flag."
	@${ECHO} " If you built with JIT=0, then the bytecode VM"
	@${ECHO} " will run by default."
	@${ECHO} " "
	@${ECHO} " To verify your build,"
	@${ECHO} " "
	@${ECHO} "   $$ make test"
	@${ECHO} " "
	@${ECHO} " Written by _why and rurban."
	@${ECHO} " Maintained at https://github.com/perl11/p2, branch p2"

config:
	@${ECHO} MAKE -f config.mak $@
	@$(MAKE) -s -f config.mak config.inc core/config.h

# bootstrap config.inc
config.inc: tools/config.sh config.mak
	@$(MAKE) -s -f config.mak $@

core/config.h: config.inc core/version.h tools/config.sh config.mak
	@$(MAKE) -s -f config.mak $@

core/version.h: config.mak $(shell git show-ref HEAD | ${SED} "s,^.* ,.git/,g")
	@$(MAKE) -s -f config.mak $@

grammar: syn/greg.y
	touch syn/greg.y
	+$(MAKE) syn/greg.c

# bootstrap syn/greg.c, syn/compile.c not yet
syn/greg.c: syn/greg.y
	@${ECHO} GREG $<
	@if test -f ${GREG}; then ${GREG} syn/greg.y > syn/greg-new.c && \
	  ${CC} ${GREGCFLAGS} -o syn/greg-new syn/greg-new.c syn/compile.c syn/tree.c -Isyn && \
	  ${MV} syn/greg-new.c syn/greg.c && \
	  ${MV} syn/greg-new syn/greg; \
	fi

core/callcc.${OBJEXT}: core/callcc.c core/p2.h core/potion.h core/config.h core/internal.h
	@${ECHO} CC $@ -O0 +frame-pointer
	@${CC} -c ${CFLAGS} -O0 -fno-omit-frame-pointer ${INCS} -o $@ $<
core/callcc.${OBJEXT}2: core/callcc.c core/p2.h core/potion.h core/config.h core/internal.h
	@${ECHO} CC $@ -O0 +frame-pointer
	@${CC} -c -DP2 ${CFLAGS} -O0 -fno-omit-frame-pointer ${INCS} -o $@ $<
ifneq (${OPIC},${OBJEXT})
core/callcc.${OPIC}: core/callcc.c core/p2.h core/potion.h core/config.h core/internal.h
	@${ECHO} CC $@ -O0 +frame-pointer
	@${CC} -c ${CFLAGS} ${FPIC} -O0 -fno-omit-frame-pointer ${INCS} -o $@ $<
core/callcc.${OPIC}2: core/callcc.c core/config.h core/p2.h core/potion.h core/internal.h
	@${ECHO} CC $@ -O0 +frame-pointer
	@${CC} -c -DP2 ${CFLAGS} ${FPIC} -O0 -fno-omit-frame-pointer ${INCS} -o $@ $<
endif

front/potion.${OBJEXT}: front/potion.c core/potion.h core/config.h core/internal.h \
 core/opcodes.h core/khash.h core/table.h
	@${ECHO} CC $@ -O0
	@${CC} -c ${CFLAGS} -O0 ${INCS} -o $@ $<
front/p2.${OBJEXT}2: front/p2.c core/p2.h core/potion.h core/config.h core/internal.h \
 core/opcodes.h core/khash.h core/table.h core/khash.h
	@${ECHO} CC $@ -O0
	@${CC} -c -DP2 ${CFLAGS} -O0 ${INCS} -o $@ $<
ifneq (${OPIC},${OBJEXT})
front/potion.${OPIC}: front/potion.c core/potion.h core/config.h core/internal.h \
 core/opcodes.h core/khash.h core/table.h
	@${ECHO} CC $@ -O0
	@${CC} -c ${CFLAGS} -O0 ${FPIC} ${INCS} -o $@ $<
front/p2.${OPIC}2: front/p2.c core/p2.h core/potion.h core/config.h core/internal.h \
 core/opcodes.h core/khash.h core/table.h core/khash.h
	@${ECHO} CC $@ -O0
	@${CC} -c -DP2 ${CFLAGS} -O0 ${FPIC} ${INCS} -o $@ $<
endif

core/potion.h: core/config.h
core/p2.h: core/potion.h
core/table.h: core/potion.h core/internal.h core/khash.h
# for c in core/*.c; do gcc -MM -D_GNU_SOURCE  -Icore $$c; done |perl -lpe's/^(.+)o:/\$(foreach o,\${OBJS},core\/$1.\${o} ):/'
$(foreach o,${OBJS},core/asm.${o} ): core/asm.c core/p2.h core/potion.h core/config.h core/internal.h \
 core/opcodes.h core/asm.h
$(foreach o,${OBJS},core/ast.${o} ): core/ast.c core/p2.h core/potion.h core/config.h core/internal.h \
 core/ast.h
$(foreach o,${OBJS},core/compile.${o} ): core/compile.c core/p2.h core/potion.h core/config.h \
 core/internal.h core/ast.h core/opcodes.h core/asm.h
$(foreach o,${OBJS},core/compile-c.${o} ): core/compile-c.c core/compile-c.h core/p2.h core/potion.h core/config.h \
 core/internal.h core/opcodes.h core/asm.h core/table.h core/khash.h
$(foreach o,${OBJS},core/contrib.${o} ): core/contrib.c core/config.h
$(foreach o,${OBJS},core/file.${o} ): core/file.c core/p2.h core/potion.h core/config.h core/internal.h \
 core/table.h core/khash.h
$(foreach o,${OBJS},core/gc.${o} ): core/gc.c core/p2.h core/potion.h core/config.h core/internal.h \
 core/gc.h core/khash.h core/table.h
$(foreach o,${OBJS},core/internal.${o} ): core/internal.c core/p2.h core/potion.h core/config.h \
 core/internal.h core/table.h core/khash.h core/gc.h
$(foreach o,${OBJS},core/lick.${o} ): core/lick.c core/p2.h core/potion.h core/config.h core/internal.h
$(foreach o,${OBJS},core/load.${o} ): core/load.c core/p2.h core/potion.h core/config.h core/internal.h \
 core/table.h core/khash.h
$(foreach o,${OBJS},core/mt19937ar.${o} ): core/mt19937ar.c core/p2.h core/potion.h core/config.h
$(foreach o,${OBJS},core/number.${o} ): core/number.c core/p2.h core/potion.h core/config.h \
 core/internal.h
$(foreach o,${OBJS},core/objmodel.${o} ): core/objmodel.c core/p2.h core/potion.h core/config.h \
 core/internal.h core/khash.h core/table.h core/asm.h
$(foreach o,${OBJS},core/primitive.${o} ): core/primitive.c core/p2.h core/potion.h core/config.h \
 core/internal.h
$(foreach o,${OBJS},core/string.${o} ): core/string.c core/p2.h core/potion.h core/config.h \
 core/internal.h core/khash.h core/table.h
$(foreach o,${OBJS},core/syntaxobj.${o} ): core/syntaxobj.c core/p2.h core/potion.h core/config.h core/internal.h
$(foreach o,${OBJS},core/table.${o} ): core/table.c core/p2.h core/potion.h core/config.h \
 core/internal.h core/khash.h core/table.h
$(foreach o,${OBJS},core/vm.${o} ): core/vm.c core/p2.h core/potion.h core/config.h \
 core/internal.h core/opcodes.h core/asm.h core/khash.h core/table.h core/vm-dis.c
$(foreach o,${OBJS},core/vm-ppc.${o} ): core/vm-ppc.c core/p2.h core/potion.h core/config.h \
 core/internal.h core/opcodes.h core/asm.h
$(foreach o,${OBJS},core/vm-x86.${o} ): core/vm-x86.c core/p2.h core/potion.h core/config.h \
 core/internal.h core/opcodes.h core/asm.h core/khash.h core/table.h
$(foreach o,${OBJS},core/vm-arm.${o} ): core/vm-arm.c core/p2.h core/potion.h core/config.h \
 core/internal.h core/opcodes.h core/asm.h core/khash.h core/table.h
$(foreach o,${OBJS},core/callcc.${o} ): core/callcc.c core/p2.h core/potion.h core/config.h \
 core/internal.h
$(foreach o,${OBJS},core/regex.${o} ): core/regex.c core/potion.h core/config.h core/table.h \
 core/khash.h
# The generated grammar objects (syntax.c is potion, syntax-p5.c and syntax-p6.c
# are p2) are rebuilt when their .y (via %.c: %.y above) or any core header
# changes. Without this, e.g. a changed struct Potion leaves stale objects.
SYN_HDRS = core/p2.h core/potion.h core/config.h core/internal.h core/opcodes.h \
 core/asm.h core/ast.h core/table.h core/khash.h core/gc.h
$(foreach o,${OBJS},syn/syntax.${o} syn/syntax-p5.${o} syn/syntax-p6.${o} ): ${SYN_HDRS}
$(foreach o,${OBJS},syn/syntax-p6.${o} ): $(wildcard syn/pvip*.h)
$(foreach o,${OBJS},lib/p6/libp6.${o} ): ${SYN_HDRS}
$(foreach o,${OBJS},$(patsubst %.c,%.${o},${P5_SRC}) ): ${SYN_HDRS} lib/p5/p5.h

# the API tests include the core headers (struct layouts): rebuild them when a header changes,
# else p2-test walks the heap with a stale sizeof(struct PNMemory)
${OBJ_TEST} ${OBJ_P2_TEST} ${OBJ_GC_TEST} ${OBJ_GC_BENCH}: core/potion.h core/p2.h core/config.h core/internal.h core/gc.h

%.i: %.c core/config.h
	@${ECHO} CPP $@
	@${CC} -c ${CFLAGS} ${INCS} -o $@ -E -c $<
%.i2: %.c core/config.h
	@${ECHO} CPP $@
	@${CC} -c -DP2 ${CFLAGS} ${INCS} -o $@ -E -c $<
%.in: %.c core/config.h
	@${ECHO} CPP ASTYLE $@
	@${CC} -c ${CFLAGS} ${INCS} -E -c $< | perl -pe's,^# (\d+) ",//# \1 ",' > $@.tmp && \
	  astyle -s2 < $@.tmp > $@
core/regex.${OBJEXT} core/regex.${OBJEXT}2: ${PCRE2_CONFIG}
ifneq (${OPIC},${OBJEXT})
core/regex.${OPIC} core/regex.${OPIC}2: ${PCRE2_CONFIG}
endif

%.in2: %.c core/config.h
	@${ECHO} CPP ASTYLE $@
	@${CC} -c -DP2 ${CFLAGS} ${INCS} -E -c $< | perl -pe's,^# (\d+) ",//# \1 ",' > $@.tmp && \
	  astyle -s2 < $@.tmp > $@
%.${OBJEXT}: %.c core/config.h
	@${ECHO} CC $@
	@${CC} -c ${CFLAGS} ${INCS} -o $@ $<
%.${OBJEXT}2: %.c core/config.h
	@${ECHO} CC $@
	@${CC} -c -DP2 ${CFLAGS} ${INCS} -o $@ $<
ifneq (${OPIC},${OBJEXT})
%.${OPIC}: %.c core/config.h
	@${ECHO} CC $@
	@${CC} -c ${FPIC} ${CFLAGS} ${INCS} -o $@ $<
%.${OPIC}2: %.c core/config.h
	@${ECHO} CC $@
	@${CC} -c -DP2 ${FPIC} ${CFLAGS} ${INCS} -o $@ $<
endif

# keep the generated grammar sources; make would delete them as intermediates
.PRECIOUS: syn/syntax-p5.c syn/syntax-p6.c

doc/p2.1: front/p2.c
	$(MAKE) bin/p2
	LC_ALL=C help2man -N -s1 -p perl11-p2 --manual "PERL11-P2 Manual $(P2_VERSION)" -o $@ bin/p2
doc/potion.1: front/potion.c
	$(MAKE) bin/potion
	LC_ALL=C help2man -N -s1 -p perl11-p2 --manual "POTION Manual $(POTION_VERSION)" -o $@ bin/potion

%.c: %.y ${GREGCROSS}
	@${ECHO} GREG $@
	@${GREGCROSS} $< > $@-new && ${MV} $@-new $@

# the native greg used when cross-compiling
syn/greg: syn/greg.c syn/compile.c syn/tree.c
	@${ECHO} HOSTCC $@
	@${HOSTCC} ${GREGCFLAGS} -o $@ syn/greg.c syn/compile.c syn/tree.c -Isyn

${GREG}: syn/greg.c syn/compile.c syn/tree.c
	@${ECHO} CC $@
	@${CC} ${GREGCFLAGS} -o $@ syn/greg.c syn/compile.c syn/tree.c -Isyn

bin/potion${EXE}: ${PIC_OBJ_POTION} lib/libpotion${DLL}
	@${ECHO} LINK $@
	@${LIBPNA_AWAY}
	@${CC} ${CFLAGS} ${LDFLAGS} ${PIC_OBJ_POTION} -o $@ ${LIBPTH} ${RPATH} -lpotion  ${LIBS}
	@${LIBPNA_BACK}
	@if [ "${DEBUG}" != "1" ]; then ${ECHO} STRIP $@; ${STRIP} $@; fi

bin/p2${EXE}: ${OBJ_P2} lib/libp2${DLL} lib/p2/libp5${LOADEXT}
	@${ECHO} LINK $@
	@${LIBP2A_AWAY}
	@${CC} ${CFLAGS} ${LDFLAGS} ${OBJ_P2} -o $@ ${LIBPTH} ${RPATH} -lp2 ${LIBS}
	@${LIBP2A_BACK}
	@if [ "${DEBUG}" != "1" ]; then ${ECHO} STRIP $@; ${STRIP} $@; fi

bin/potion-s${EXE}: lib/libpotion.a ${PLIBS_OBJS}
	@${ECHO} LINK $@
	@${CC} -c ${CFLAGS} ${INCS} -DSTATIC -o front/potion.${OBJEXT}s front/potion.c
	@${CC} ${CFLAGS} ${LDFLAGS} front/potion.${OBJEXT}s -o $@ ${PLIBS_OBJS} \
          lib/libpotion.a ${LIBPTH} ${RPATH} ${EXTLIBS} ${LIBS}
	@if [ "${DEBUG}" != "1" ]; then ${ECHO} STRIP $@; ${STRIP} $@; fi
	@if [ "${SANDBOX}" = "1" ]; then rm bin/potion${EXE}; cd bin; ln -s potion-s${EXE} potion${EXE}; cd ..; fi

bin/p2-s${EXE}: lib/libp2.a ${PLIBS_OBJS2} ${P5_OBJS2}
	@${ECHO} LINK $@
	@${CC} -c ${CFLAGS} ${INCS} -DSTATIC -DP2 -o front/p2.${OBJEXT}s front/p2.c
	@${CC} ${CFLAGS} ${LDFLAGS} front/p2.${OBJEXT}s -o $@ ${PLIBS_OBJS2} ${P5_OBJS2} \
          lib/libp2.a ${LIBPTH} ${RPATH} ${EXTLIBS} ${LIBS}
	@if [ "${DEBUG}" != "1" ]; then ${ECHO} STRIP $@; ${STRIP} $@; fi
	@if [ "${SANDBOX}" = "1" ]; then rm bin/p2${EXE}; cd bin; ln -s p2-s${EXE} p2${EXE}; cd ..; fi

lib/readline/readline.${OBJEXT} lib/readline/readline.${OBJEXT}2: lib/readline/readline.c lib/readline/linenoise.c
	@${ECHO} CC $@
	@${LIBPNA_AWAY}
	@$(MAKE) -s -C lib/readline static
	@ln -sf readline.${OBJEXT} lib/readline/readline.${OBJEXT}2
	@${LIBPNA_BACK}

lib/libpotion.a: ${OBJ_SYN} ${OBJ} ${PCRE2_LIB} core/config.h core/potion.h
	@${ECHO} AR $@
	@if [ -e $@ ]; then rm -f $@; fi
	@${AR} rcs $@ ${OBJ_SYN} ${OBJ} > /dev/null
	@${ECHO} RANLIB $@
	@-${RANLIB} $@

lib/libp2.a: ${OBJ_P2_SYN} ${OBJ2} ${PCRE2_LIB} core/config.h core/potion.h
	@${ECHO} AR $@
	@if [ -e $@ ]; then rm -f $@; fi
	@${AR} rcs $@ ${OBJ_P2_SYN} ${OBJ2} > /dev/null
	@${ECHO} RANLIB $@
	@-${RANLIB} $@

lib/libpotion${DLL}: ${PIC_OBJ} ${PIC_OBJ_SYN} ${EXTLIBDEPS} ${PCRE2_LIB} core/config.h core/potion.h
	@${ECHO} LD $@
	@if [ -e $@ ]; then rm -f $@; fi
	@${CC} ${DEBUGFLAGS} -o $@ ${LDDLLFLAGS} ${RPATH} \
	  ${PIC_OBJ} ${PIC_OBJ_SYN} ${LIBPTH} ${LIBS} > /dev/null
	@if [ x${DLL} = x.dll ]; then cp $@ bin/; fi

lib/libp2${DLL}: $(subst .${OPIC},.${OPIC}2,${PIC_OBJ}) ${PIC_OBJ_P2_SYN} ${EXTLIBDEPS} ${PCRE2_LIB} core/config.h core/potion.h
	@${ECHO} LD $@
	@if [ -e $@ ]; then rm -f $@; fi
	@${CC} ${DEBUGFLAGS} -o $@ $(subst libpotion,libp2,${LDDLLFLAGS}) ${RPATH} \
	  $(subst .${OPIC},.${OPIC}2,${PIC_OBJ}) ${PIC_OBJ_P2_SYN} ${LIBPTH} ${LIBS} > /dev/null
	@if [ x${DLL} = x.dll ]; then cp $@ bin/; fi

lib/potion/libsyntax${DLL}: syn/syntax.${OPIC} lib/libpotion${DLL}
	@${ECHO} LD $@
	@$(CC) ${DEBUGFLAGS} -o $@ $(INCS) ${LDDLLFLAGS} ${RPATH} \
	  $< ${LIBPTH} -lpotion $(LIBS)

lib/p2/libsyntax-p5${DLL}: syn/syntax-p5.${OPIC}2 lib/libp2${DLL}
	@${ECHO} LD $@
	@${CC} ${DEBUGFLAGS} -o $@ $(INCS) $(subst libpotion,potion/libsyntax-p5,${LDDLLFLAGS}) \
	  $< ${LIBPTH} -lp2 $(LIBS)

lib/p2/libsyntax-p6${LOADEXT}: syn/syntax-p6.${OPIC}2 $(wildcard syn/pvip*.c syn/pvip*.h) lib/libp2${DLL}
	@${ECHO} LD $@
	${CC} ${DEBUGFLAGS} -o $@ $(INCS) $(subst libpotion,potion/libsyntax-p6,${LDDLLFLAGS}) \
	  ${RPATH} $< syn/pvip*.c ${LIBPTH} -lp2 $(LIBS)


# p5 runtime: natives for the p5 syntax, kept out of the core
lib/p2/libp5${LOADEXT}: ${P5_SRC} lib/p5/p5.h ${SYN_HDRS} lib/libp2${DLL}
	@${ECHO} LD $@
	@${CC} ${DEBUGFLAGS} -o $@ $(INCS) -Ilib/p5 $(subst libpotion,p2/libp5,${LDDLLFLAGS}) ${RPATH} ${P5_SRC} ${LIBPTH} -lp2 $(LIBS)

# p6 runtime
lib/p2/libp6${LOADEXT}: lib/p6/libp6.c lib/p6/p6test.c lib/p6/p6core.c ${SYN_HDRS} lib/libp2${DLL}
	@${ECHO} LD $@
	${CC} ${DEBUGFLAGS} -o $@ $(INCS) ${LDDLLFLAGS} ${RPATH} $< ${LIBPTH} -lp2 $(LIBS)
# 3rdparty EXTLIBS statically linked
3rd/libuv/Makefile.am: .gitmodules
	git submodule update --init
	@touch $@

ifeq (${WIN32},1)
PATCH_PHLPAPI2 = sed -i -e"s,-lphlpapi2,-liphlpapi," 3rd/libuv/Makefile.am
CROSSHOST = --host=$(shell ${CC} -v 2>&1 | sed -n 's/^Target: //p')
LIBUV_CPPFLAGS = CPPFLAGS="${CPPFLAGS} -DBUILDING_UV_SHARED"
else
PATCH_PHLPAPI2 = echo
CROSSHOST =
LIBUV_CPPFLAGS =
PWD = $(shell pwd)
endif

ifeq (${APPLE},1)
3rd/libuv/%.p2.${OBJEXT}: 3rd/libuv/%.c ${LIBUV_HEADERS}
	@${ECHO} CC $@
	@${CC} -c ${CFLAGS} ${FPIC} -DBUILDING_UV_SHARED \
	  -D_DARWIN_USE_64_BIT_INODE=1 -D_DARWIN_UNLIMITED_SELECT=1 \
	  -I3rd/libuv/include -I3rd/libuv/src -I3rd/libuv/src/unix -o $@ $<

lib/libuv.a: ${LIBUV_OBJ}
	@${ECHO} AR $@
	@rm -f $@
	@${AR} rcs $@ ${LIBUV_OBJ}

${LIBUV}: ${LIBUV_OBJ}
	@${ECHO} LD $@
	@rm -f $@
	@${CC} ${DEBUGFLAGS} -dynamiclib \
	  -install_name @executable_path/../lib/libuv${DLL} -o $@ ${LIBUV_OBJ} -lpthread
else
3rd/libuv/Makefile: 3rd/libuv/Makefile.am
	@${ECHO} AUTOGEN $@
	@${PATCH_PHLPAPI2}
	cd 3rd/libuv && ./autogen.sh
	-grep "libuv 1." 3rd/libuv/configure && sed -i -e's,libuv 1.,libuv-1.,' 3rd/libuv/configure
	cd 3rd/libuv && ${LIBUV_CPPFLAGS} CC="${CC}" ./configure \
	  --enable-shared --prefix="${PWD}" "${CROSSHOST}"
	@-rm -f 3rd/libuv/configure~

lib/libuv.a: config.inc 3rd/libuv/Makefile
	@${ECHO} MAKE $@
	+$(MAKE) -s -C 3rd/libuv libuv.la
	cp 3rd/libuv/.libs/libuv.a lib/
	@touch $@

# default: shared
${LIBUV}: config.inc 3rd/libuv/Makefile
	@${ECHO} MAKE $@
	+$(MAKE) -s -C 3rd/libuv libuv.la
	cp 3rd/libuv/.libs/libuv*${DLL}* lib/
	@touch $@
endif


ifneq (,$(filter 1,${APPLE} ${WIN32}))
# Build PCRE2 directly from its portable C sources. Upstream git checkouts do
# not contain configure, and macOS does not provide the libtoolize needed by
# autogen.sh.
${PCRE2_CONFIG}: ${PCRE2_DIR}/pcre2.h.generic
	@${ECHO} GEN $@
	@cp $< $@
${PCRE2_INTERNAL_CONFIG}: ${PCRE2_DIR}/config.h.generic
	@${ECHO} GEN $@
	@cp $< $@


${PCRE2_CHARTABLES}: ${PCRE2_DIR}/pcre2_chartables.c.dist
	@${ECHO} GEN $@
	@cp $< $@

${PCRE2_DIR}/%.p2.${OBJEXT}: ${PCRE2_DIR}/%.c ${PCRE2_CONFIG} \
 ${PCRE2_INTERNAL_CONFIG} ${PCRE2_HEADERS}
	@${ECHO} CC $@
	@${CC} -c ${CFLAGS} ${FPIC} -DHAVE_CONFIG_H \
	  -DPCRE2_CODE_UNIT_WIDTH=8 -DPCRE2_STATIC -DSUPPORT_PCRE2_8 \
	  -DSUPPORT_UNICODE -I${PCRE2_DIR} -o $@ $<

${PCRE2_LIB}: ${PCRE2_OBJ}
	@${ECHO} AR $@
	@mkdir -p 3rd/pcre/.libs
	@rm -f $@
	@${AR} rcs $@ ${PCRE2_OBJ}
else
3rd/pcre/configure:
	@${ECHO} AUTOGEN PCRE2
	@cd 3rd/pcre && ./autogen.sh >/dev/null

${PCRE2_CONFIG}: 3rd/pcre/configure
	@${ECHO} CONFIGURE PCRE2
	@cd 3rd/pcre && CC="${CC}" CFLAGS="-g -O2 ${FPIC}" ./configure --disable-shared --enable-static \
	  --with-pic --enable-pcre2-8 --disable-jit

${PCRE2_LIB}: ${PCRE2_CONFIG}
	@${ECHO} MAKE PCRE2
	@$(MAKE) -s -C 3rd/pcre libpcre2-8.la
endif


# DYNLIBS
lib/potion/readline${LOADEXT}: core/config.h core/potion.h \
  lib/readline/Makefile lib/readline/linenoise.c \
  lib/readline/linenoise.h lib/libpotion${DLL} lib/libp2${DLL}
	@${ECHO} MAKE $@
	@${LIBPNA_AWAY}
	@$(MAKE) -s -C lib/readline
	@${LIBPNA_BACK}
	@cp lib/readline/readline${LOADEXT} $@

lib/potion/buffile${LOADEXT}: core/config.h core/potion.h \
  lib/buffile.${OPIC} lib/buffile.c lib/libpotion${DLL}
	@${ECHO} LD $@
	@${LIBPNA_AWAY}
	@${CC} $(DEBUGFLAGS) -o $@ $(subst libpotion,potion/buffile,${LDDLLFLAGS}) ${RPATH} \
	  lib/buffile.${OPIC} ${LIBPTH} -lpotion ${LIBS} > /dev/null
	@${LIBPNA_BACK}

lib/p2/buffile${LOADEXT}: core/config.h core/potion.h core/p2.h \
  lib/buffile.${OPIC}2 lib/buffile.c lib/libp2${DLL}
	@${ECHO} LD $@
	@${LIBP2A_AWAY}
	@${CC} $(DEBUGFLAGS) -o $@ $(subst libpotion,p2/buffile,${LDDLLFLAGS}) ${RPATH} \
	  lib/buffile.${OPIC}2 ${LIBPTH} -lp2 ${LIBS} > /dev/null
	@${LIBP2A_BACK}

ifeq ($(HAVE_LIBUV),1)
AIO_DEPS =
#TODO: libtool?
AIO_DEPLIBS =
else
AIO_DEPS = ${LIBUV}
ifeq (${APPLE},1)
AIO_DEPLIBS =
else
AIO_DEPLIBS := `perl -ane'/dependency_libs=(.*)/ && print substr($$1,2,-1)' 3rd/libuv/libuv.la`
endif
endif

lib/potion/aio${LOADEXT}: core/config.h core/potion.h \
  lib/aio.c $(AIO_DEPS) lib/libpotion${DLL}
	@${ECHO} CC lib/aio.${OPIC}
	@${CC} -c ${FPIC} ${CFLAGS} ${INCS} -o lib/aio.${OPIC} lib/aio.c > /dev/null
	@${ECHO} LD $@
	@${LIBPNA_AWAY}
	@${CC} $(DEBUGFLAGS) -o $@ $(subst libpotion,potion/aio,${LDDLLFLAGS}) ${RPATH} \
	  lib/aio.${OPIC} ${LIBPTH} -lpotion ${EXTLIBS} ${LIBS} ${AIO_DEPLIBS} > /dev/null
	@${LIBPNA_BACK}

lib/p2/aio${LOADEXT}: core/config.h core/potion.h \
  lib/aio.c $(AIO_DEPS) lib/libp2${DLL}
	@${ECHO} CC lib/aio.${OPIC}2
	@${CC} -c ${FPIC} -DP2 ${CFLAGS} ${INCS} -o lib/aio.${OPIC}2 lib/aio.c > /dev/null
	@${ECHO} LD $@
	@${LIBP2A_AWAY}
	@${CC} $(DEBUGFLAGS) -o $@ $(subst libp2,p2/aio,${LDDLLFLAGS}) ${RPATH} \
	  lib/aio.${OPIC}2 ${LIBPTH} -lp2 ${EXTLIBS} ${LIBS} ${AIO_DEPLIBS} > /dev/null
	@${LIBP2A_BACK}


lib/p2/m_apm${LOADEXT}: core/config.h core/potion.h \
  lib/m_apm/Makefile lib/libpotion${DLL}
	@${ECHO} MAKE $@
	@${LIBP2A_AWAY}
	@$(MAKE) -s -C lib/m_apm
	@${LIBP2A_BACK}
	@cp lib/m_apm/m_apm${LOADEXT} $@

# the time keyword (console format real/user/sys) is a bash builtin; with dash
# make would run the external /usr/bin/time and print its own format
bench: SHELL = bash
bench.summary: bin/p2${EXE} bin/potion${EXE}
	RUNS=3 test/bench-summary.sh
bench: bin/gc-bench${EXE} bin/potion${EXE}
	test/bench-summary.sh
	@${ECHO} running GC benchmark; time bin/gc-bench
	for e in `ls example/*.pn|grep -vE 'example/(24_game|ackermann|fib|nqueens)\.pn'`; do echo $$e; time bin/potion $$e; done
	for e in `ls example/*.pn|grep -vE 'example/(24_game|ackermann|fib|nqueens)\.pl'`; do echo $$e; time bin/p2 $$e; time perl $$e; done
	for e in example/*.p2; do echo $$e; time bin/p2 $$e; done

check: test
test:  test.pn test.p2 test.p6 test.p5 test.roast6 test.compile-c test.repl

test.pn: pn libs testable
	+test/runtests.sh -q -pn


# roast5 files do require './test.pl' from the cwd; test/roast5.sh links the real
# file (test/p5/test.pl) in for the run and removes it again.
test.p2: p2 libs testable
	+test/runtests.sh -q -p2
	@if [ -d test/roast5 ]; then \
	  ${ECHO} test/roast5.sh -e bin/p2; \
	  test/roast5.sh -e bin/p2; \
        else ${ECHO} test/roast5 missing; \
        fi


test.roast6: bin/p2${EXE} libs
	@if [ -d test/roast6 ]; then \
	  ${ECHO} test/roast6.sh; \
	  test/roast6.sh; \
	else ${ECHO} test/roast6 missing; fi

test.p6: bin/p2${EXE} libs
	${ECHO} "running test.p6"
	@rc=0; for f in test/p6/*.p6; do \
	  test/run_p6_test.sh $$f || rc=1; \
	done; exit $$rc

# test/p5 has two conventions: older *.t files with a sibling
# *.expected file (full-stdout comparison via run_p5_test.sh), and
# marker-based *.t files whose '#=> expected' comments are compared
# against stdout by runtests.sh -p5 (same convention as test/*.pl).
test.p5: bin/p2${EXE} libs
	@rc=0; marker=; for f in test/p5/*.t; do \
	  case $$f in *-unix.t) if test "${WIN32}" = 1; then continue; fi;; esac; \
	  if test -f $${f%.t}.expected; then \
	    test/run_p5_test.sh $$f || rc=1; \
	  else \
	    marker="$$marker $$f"; \
	  fi; \
	done; \
	${ECHO} "running test.p5: `for f in $$marker; do basename $$f .t; done | tr '\n' ' '`"; \
	test/runtests.sh -q -p5 $$marker || rc=1; \
	exit $$rc

run_p6: test.p6
testable : bin/potion${EXE} bin/p2${EXE} libs bin/potion-test${EXE} bin/p2-test${EXE} bin/gc-test${EXE}

.PHONY: test.compile-c
test.compile-c: bin/potion${EXE} bin/p2${EXE} lib/libpotion.a lib/p2/libp6${LOADEXT} lib/p2/libsyntax-p6${LOADEXT}
	POTION_CC="${CC}" POTION_CFLAGS="${CFLAGS}" POTION_LDFLAGS="${LDFLAGS}" POTION_LIBPTH="${LIBPTH}" POTION_INCS="${INCS}" \
	  POTION_LOADEXT="${LOADEXT}" test/compile-c.sh

.PHONY: test.repl
test.repl: bin/p2${EXE}
	test/repl.sh

spectest_checkout : test/spec
test/spec :
	@${ECHO} UPDATE $@
	@git submodule update --init test/spec

spectest_init :
	@git submodule add https://github.com/rakudo-p5/roast5 test/spec

spectest_update :
	-cd test/spec && git pull

spectest: testable test/spectest.data
	 perl test/harness --tests-from-file=test/spectest.data

fulltest: testable test/spectest.data
	 perl test/harness test/spec/*.t test/spec/*/*.t

# for LTO gold -O4
bin/potion-test${EXE}: ${OBJ_TEST} lib/libpotion.a
	@${ECHO} LINK $@
	@if ${CC} ${CFLAGS} ${LDFLAGS} ${OBJ_TEST} -o $@ lib/libpotion.a ${RPATH} ${LIBPTH} ${EXTLIBS} ${LIBS}; then true; else \
	  ${CC} ${CFLAGS} ${LDFLAGS} ${OBJ_TEST} -o $@ ${OBJ} ${OBJ_SYN} ${LIBPTH} ${LIBS}; fi

bin/gc-test${EXE}: ${OBJ_GC_TEST} lib/libp2.a
	@${ECHO} LINK $@
	@${CC} ${CFLAGS} ${LDFLAGS} ${OBJ_GC_TEST} -o $@ lib/libp2.a ${RPATH} ${LIBPTH} ${EXTLIBS} ${LIBS}

bin/gc-bench${EXE}: ${OBJ_GC_BENCH} lib/libp2.a
	@${ECHO} LINK $@
	@${CC} ${CFLAGS} ${LDFLAGS} ${OBJ_GC_BENCH} -o $@ lib/libp2.a ${RPATH} ${LIBPTH} ${EXTLIBS} ${LIBS}

bin/p2-test${EXE}: ${OBJ_P2_TEST} lib/libp2.a
	@${ECHO} LINK $@
	@if ${CC} ${CFLAGS} ${LDFLAGS} ${OBJ_P2_TEST} -o $@ lib/libp2.a ${RPATH} ${LIBPTH} ${EXTLIBS} ${LIBS}; then true; else \
	  ${CC} ${CFLAGS} ${LDFLAGS} ${OBJ_P2_TEST} -o $@ ${OBJ2} ${OBJ_P2_SYN} ${RPATH} ${LIBPTH} ${EXTLIBS} ${LIBS}; fi

examples: pn p2
	for e in example/*.pn; do echo $$e; time bin/potion $$e; done
	for e in example/*.pl; do echo $$e; time bin/p2 $$e; done

dist: bins libs $(AIO_DEPS) static ${SRC_SYN} ${SRC_P2_SYN} ${GREG}
	+@if [ -n "${RPATH}" ]; then \
	  rm -f ${BINS} ${PNLIB}; \
	  $(MAKE) bins libs RPATH="${RPATH_INSTALL}"; \
	fi
	+$(MAKE) -f dist.mak $@ PREFIX="${PREFIX}" EXE=${EXE} DLL=${DLL} LOADEXT=${LOADEXT}

install: bins libs $(AIO_DEPS) ${GREG}
	+$(MAKE) -f dist.mak $@ PREFIX="${PREFIX}"

tarball:
	+$(MAKE) -f dist.mak $@ PREFIX="${PREFIX}"

release: dist
	+$(MAKE) -f dist.mak $@ PREFIX="${PREFIX}"

%.html: %.md doc/logo
	@${ECHO} DOC $@
	@${ECHO} "<!DOCTYPE html>" > $@
	@${ECHO} "<html lang=\"en\">" >> $@
	@${ECHO} "<head><meta charset=\"utf-8\" />" >> $@
	@${ECHO} "<style type=\"text/css\">@import 'doc.css';</style>" >> $@
	@${ECHO} "</head><body>" >> $@
	@${CAT} doc/logo >> $@
	@${ECHO} "<div id='central'>" >> $@
	@pandoc -f gfm -t html $< >> $@
	@${ECHO} "</div></body></html>" >> $@

MANIFEST:
	git ls-tree -r --name-only HEAD > $@

doc: ${DOCHTML} doc/html/files.html
ifeq (${CYGWIN},1)
CHM = doc/html/p2.chm
else
CHM =
endif

docall: doc GTAGS ${CHM}
chm: ${CHM}
DOXY_PRE = doc/footer.sh > doc/footer.inc

doxygen: ${DOCHTML} doc/html/files.html
	@${ECHO} DOXYGEN -f core lib
	@${DOXY_PRE}
	@doxygen doc/Doxyfile
doc/html/index.hhp: doc/html/files.html doc/Doxyfile.chm
	@${ECHO} DOXYGEN doc/html/index.hhp
	@${DOXY_PRE}
	-rm -rf doc/html/*
	@doxygen doc/Doxyfile.chm
doc/html/p2.chm: doc/html/index.hhp
	@${ECHO} HHC $@
	-cd doc/html; PATH=/cygdrive/c/Program\ Files/HTML\ Help\ Workshop:$PATH hhc index.hhp
doc/html/files.html: ${SRC} doc/Doxyfile doc/footer.sh Makefile
	@${ECHO} DOXYGEN core
	@${DOXY_PRE}
	-rm -rf doc/html/*
	@doxygen doc/Doxyfile 2>&1 | perl -0777 -pe "s/^[^\n]*warning: The following parameters? of [^\n]*\n(?:  parameter '(?:P|cl|self)'\n)+//mg"

# perl11.com admins only. requires: doxygen pandoc global
website:
	test -d ${WEBSITE} || exit
	@$(MAKE) doxygen
	cp -r doc/html/* ${WEBSITE}/p2/html/
	@$(MAKE) doc
	cp doc/*.html ${WEBSITE}/p2/
	@$(MAKE) GTAGS
	cp -r HTML/* ${WEBSITE}/p2/ref/
	cd ${WEBSITE}/p2/ && git add *.html html ref && git ci -m'doc: automatic update'
	@${ECHO} "need to cd ${WEBSITE}; git push"

# in seperate clean subdir. do not index work files
GTAGS: ${SRC} core/*.h
	+$(MAKE) -f dist.mak $@ PREFIX=${PREFIX}

TAGS: ${SRC} core/*.h
	@rm -f TAGS
	/usr/bin/find core lib syn front \( -name \*.c -o -name \*.h \) -exec etags -a --language=c \{\} \;

sloc: clean
	@rm -rf dist
	@git checkout-index --prefix=dist/ -a
	@cd dist && \
	  rm syn/greg.c && \
	  sloccount core syn front lib && \
	  cd ..  && \
	rm -rf dist

todo:
	@grep -rInso 'TODO: \(.\+\)' core syn front lib tools

clean:
	@${ECHO} cleaning
	@rm -f $(foreach ext,${OBJEXT} ${OBJEXT}2 opic opic2 i gcda gcno,$(foreach dir,core syn front lib test/api lib/* 3rd/libtommath,${dir}/*.${ext}))
	@rm -f bin/* lib/libpotion.* lib/potion/*${DLL} lib/*/*${LOADEXT} lib/*/*.${OBJEXT} front/*.${OBJEXT}s
	@rm -rf lib/*/*.bundle.dSYM
	@rm -f lib/libp2.* lib/p2/*${DLL}
	@rm -f lib/potion/*.pnb lib/potion/*/*.pnb
	@rm -f lib/p2/*.plc lib/p2/*/*.plc
	@rm -f ${DOCHTML} doc/footer.inc
	@rm -f tools/*.${OBJEXT} core/config.h core/version.h
	@rm -f ${PCRE2_OBJ}
	@rm -f ${LIBUV_OBJ}
	@rm -f tools/*~ doc/*~ example/*~ core/*~ config.inc~ tools/config.c
	@rm -rf doc/latex
	@rm -f 3rd/libuv/configure~
	@-cd test/roast5 && git clean -dxf; cd -

# also config.inc and files needed for cross-compilation
realclean: clean
	@rm -f config.inc ${GREG} ${GREGCROSS} ${SRC_SYN} ${SRC_P2_SYN}
	@rm -f GPATH GTAGS GRTAGS
	@rm -rf doc/ref doc/html
	@rm -rf lib/*${DLL} lib/*${LOADEXT} lib/*.a
	$(MAKE) -C 3rd/libuv clean
	$(MAKE) -C 3rd/libtommath clean
	$(MAKE) -C 3rd/pcre distclean
	@if test -f 3rd/libuv/Makefile.am; then rm 3rd/libuv/Makefile; fi
	@# a cross build (mingw) leaves a pcre that does not link natively (___chkstk_ms)
	@if test -f 3rd/pcre/Makefile; then $(MAKE) -s -C 3rd/pcre distclean >/dev/null 2>&1; fi
	@rm -f 3rd/pcre/Makefile 3rd/pcre/config.status 3rd/pcre/pcre2.h
	@find . -name \*.gcov -delete

test.c: bin/potion${EXE}
	f=test/classes/creature.pn && \
	look=`${CAT} $$f | ${SED} "/\#=>/!d; s/.*\#=> //"` && \
	for=`bin/potion -I -B $$f | ${SED} "s/\n$$//"` && \
	if [ "$$look" != "$$for" ]; then \
	  ${ECHO} "$$f: expected <$$look>, but got <$$for>"; \
	fi

test.u: bin/p2${EXE}
	f=test/closures/upvals.pl && \
	look=`${CAT} $$f | ${SED} "/\#=>/!d; s/.*\#=> //"` && \
	for=`bin/p2 -I -X $$f | ${SED} "s/\n$$//"` && \
	if [ "$$look" != "$$for" ]; then \
	  ${ECHO} "$$f: expected <$$look>, but got <$$for>"; \
	fi
