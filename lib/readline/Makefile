include ../../config.inc
SRC   = linenoise.c
ifeq ($(WIN32),1)
  SRC  += win32fixes.c
  LIBS += -lws2_32
endif

all: readline${LOADEXT}

static: readline.o linenoise.o

%.o: %.c
	@$(CC) -c $(CFLAGS) -o $@ $(INCS) $<

%${LOADEXT}: %.c
	@if [ -f ../../libp2.a ]; then mv ../../libp2.a ../../libp2.a.tmp; fi
	@$(CC) $(CFLAGS) -o $@ $(INCS) $(LDDLLFLAGS) $(SRC) $< ${LIBPTH} -lp2 $(LIBS)
	@if [ -f ../../libp2.a.tmp ]; then mv ../../libp2.a.tmp ../../libp2.a; fi

test:

clean:
	@rm -f readline${LOADEXT} *.o
