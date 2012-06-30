ENV_PATH=/bin:/usr/bin
ENV_SUPATH=/sbin:/bin:/usr/sbin:/usr/bin
CONFIG_PATH=/etc/chpersroot.conf

CFLAGS=-g -O2 -Wall
INSTALL=/usr/bin/install

prefix=/usr/local

-include config.mak

CFLAGS+= -Isrc

CFLAGS+= -DENV_PATH=\"$(ENV_PATH)\"
CFLAGS+= -DENV_SUPATH=\"$(ENV_SUPATH)\"
CFLAGS+= -DCONFIG_PATH=\"$(CONFIG_PATH)\"

ifndef bindir
bindir=$(prefix)/bin
endif

.PHONY: all clean install check

all: chpersroot

clean:
	$(RM) chpersroot src/*.o

install: chpersroot
	$(INSTALL) -m 4755 -o root chpersroot $(bindir)

src/configfile.o: src/configfile.c src/configfile.h src/iniparser.h
src/copyfile.o: src/copyfile.c src/copyfile.h
src/chpersroot.o: src/chpersroot.c src/copyfile.h
src/iniparser.o: src/iniparser.c src/iniparser.h

chpersroot: src/chpersroot.o src/copyfile.o src/configfile.o src/iniparser.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

test/initest: src/iniparser.o test/initest.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

check: test/initest
	@$(SH) test/t-iniparser.sh
