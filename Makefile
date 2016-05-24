ASTMODDIR=/usr/lib64/asterisk/modules
MODULES=codec_opus format_vp8
LIBS=

.PHONY: all clean install uninstall $(MODULES)

all: $(MODULES)

%.so: %.c
	gcc -o $@ -pthread -g3 -O3 -D_FORTIFY_SOURCE=2 -fPIC -DAST_MODULE=\"$(*F)\" -shared -Wl,--warn-common $< $(LIBS)

clean:
	rm -f */*.so

install: $(MODULES)
	cp */*.so $(ASTMODDIR)

uninstall:
	cd $(ASTMODDIR) && rm $(addsuffix .so,$(MODULES))

codec_opus: LIBS=-lopus
codec_opus: codecs/codec_opus.so

format_vp8: formats/format_vp8.so
