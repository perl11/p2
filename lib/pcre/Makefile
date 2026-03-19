include ../../config.inc
INCS += -I../../core -I../../3rd/pcre
LIBS += -L.. -lp2 -L../../3rd/pcre -lpcre
SRC   =

all: pcre${LOADEXT}

%${LOADEXT}: %.c
	@if [ -f ../../libp2.a ]; then mv ../../libp2.a ../../libp2.a.tmp; fi
	@$(CC) $(CFLAGS) -o $@ $(INCS) $(LDDLLFLAGS) $(SRC) $< $(LIBS)
	@if [ -f ../../libp2.a.tmp ]; then mv ../../libp2.a.tmp ../../libp2.a; fi

test:

clean:
	@rm -f ../pcre${LOADEXT} pcre${LOADEXT} *.o
