CC = gcc
RC = windres

DEFS = -DHAVE_CONFIG_H
LIBS = -lcurses -lm -lz 
TARGET = atari800

CFLAGS = -O2 -Wall
LDFLAGS = -s

INSTALL = /usr/bin/install -c
INSTALL_PROGRAM = ${INSTALL} -s
INSTALL_DATA = ${INSTALL} -m 644

BIN_DIR = /usr/local/bin
MAN_DIR = /usr/local/share/man/man1
DOC_DIR = /usr/local/share/doc/atari800

DESTDIR =

OBJS = \
	antic.o \
	atari.o \
	binload.o \
	cartridge.o \
	cassette.o \
	compfile.o \
	cpu.o \
	devices.o \
	gtia.o \
	log.o \
	memory.o \
	monitor.o \
	pbi.o \
	pia.o \
	pokey.o \
	rtime.o \
	sio.o \
	util.o \
	atari_curses.o input.o statesav.o ui_basic.o ui.o pokeysnd.o mzpokeysnd.o remez.o sndsave.o sound_oss.o



all: $(TARGET)

%.o: %.c
	$(CC) -c -o $@ $(DEFS) -I. $(CFLAGS) $<

%.ro: %.rc
	$(RC) --define WIN32 --define __MINGW32__ --include-dir . $< $@

%.o: %.cpp
	$(CC) -c -o $@ $(DEFS) -I. $(CFLAGS) $<

%.o: %.asm
	cd $(<D); xgen -L1 $(@F) $(<F)
	cd $(<D); gst2gcc gcc $(@F)

$(TARGET): $(OBJS)
	$(CC) -o $@ $(LDFLAGS) $(OBJS) $(LIBS)

dep:
	@if ! makedepend -Y $(DEFS) -I. ${OBJS:.o=.c} 2>/dev/null; \
	then echo warning: makedepend failed; fi

clean:
	rm -f *.o dos/*.o falcon/*.o win32/*.o win32/*.ro $(TARGET) core *.bak *~

distclean: clean
	-rm -f Makefile configure config.log config.status config.h
	-rm -rf autom4te.cache

install: $(TARGET) installdirs
	$(INSTALL_PROGRAM) $(TARGET) ${DESTDIR}${BIN_DIR}/$(TARGET)
	$(INSTALL_DATA) atari800.man ${DESTDIR}${MAN_DIR}/atari800.1
# install also the documentation
	$(INSTALL_DATA) ../COPYING     ${DESTDIR}${DOC_DIR}/COPYING
	$(INSTALL_DATA) ../README.1ST  ${DESTDIR}${DOC_DIR}/README.1ST
	$(INSTALL_DATA) ../DOC/README  ${DESTDIR}${DOC_DIR}/README
	$(INSTALL_DATA) ../DOC/INSTALL ${DESTDIR}${DOC_DIR}/INSTALL
	$(INSTALL_DATA) ../DOC/USAGE   ${DESTDIR}${DOC_DIR}/USAGE
	$(INSTALL_DATA) ../DOC/NEWS    ${DESTDIR}${DOC_DIR}/NEWS

install-svgalib: install
	chown root.root ${DESTDIR}${BIN_DIR}/$(TARGET)
	chmod 4755 ${DESTDIR}${BIN_DIR}/$(TARGET)

readme.html: $(TARGET)
	./$(TARGET) -help </dev/null | ../util/usage2html.pl \
		../DOC/readme.html.in ../DOC/USAGE ./atari.h > $@

doc: readme.html

installdirs:
	mkdir -p $(DESTDIR)$(BIN_DIR) $(DESTDIR)$(MAN_DIR) $(DESTDIR)$(DOC_DIR)
