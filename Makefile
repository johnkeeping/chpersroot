CFLAGS=-g -O2 -Wall
INSTALL=/usr/bin/install

prefix=/usr/local

-include config.mak

ifndef PROG_NAME
$(error you must set PROG_NAME in config.mak)
endif
ifndef ROOT_DIR
$(error you must set ROOT_DIR in config.mak)
endif

ifndef bindir
bindir=$(prefix)/bin
endif

CFLAGS+=-DROOT_DIR=\"$(ROOT_DIR)\"

.PHONY: all clean install

all: chpersroot

clean:
	$(RM) chpersroot src/*.o

install: chpersroot
	$(INSTALL) -m 6755 -o root -T chpersroot $(bindir)/$(PROG_NAME)

chpersroot: src/chpersroot.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)
