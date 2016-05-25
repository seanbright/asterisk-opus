prefix=/usr/local
exec_prefix=$(prefix)
libdir=$(exec_prefix)/lib

CC=gcc
CFLAGS=-pthread -g3 -O3 -D_FORTIFY_SOURCE=2 -fPIC
CPPFLAGS=
DEFS=
INSTALL=/usr/bin/install -c
LDFLAGS=-shared -pthread -Wl,--warn-common
LIBS=
MKDIR_P=/bin/mkdir -p
SHELL=/bin/sh

ASTMODDIR=$(libdir)/asterisk/modules
MODULES=codec_opus format_vp8

.SUFFIXES: .c .so

.PHONY: all clean install uninstall $(MODULES)

all: $(MODULES)

clean:
	rm */*.so

install: $(MODULES)
	$(MKDIR_P) $(ASTMODDIR)
	$(INSTALL) */*.so $(ASTMODDIR)

uninstall:
	cd $(ASTMODDIR) && rm $(addsuffix .so,$(MODULES))

codec_opus: LIBS+=-lopus
codec_opus: DEFS+=-DAST_MODULE=\"codec_opus\"
codec_opus: codecs/codec_opus.so

format_vp8: DEFS+=-DAST_MODULE=\"format_vp8\"
format_vp8: formats/format_vp8.so

.c.so:
	$(CC) -o $@ $(DEFS) $(CPPFLAGS) $(CFLAGS) $(LIBS) $(LDFLAGS) $<
