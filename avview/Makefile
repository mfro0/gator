
X11=/usr/X11R6/

INCLUDES=-I$(X11)/include 
X11LIBS=$(X11)/lib

DEFINES=

CC=gcc -pipe -g $(INCLUDES) $(DEFINES)

OBJS=xv.c frequencies.c xmisc.c

LIBS=-lm -ldl -L$(X11LIBS) -lX11 -lXext -lXv 

TCLLIBS=-ltcl8.3 
TKLIBS=-ltk8.3


all: avview 
	sync
	bash -c 'export DISPLAY=:0.0; ./avview.scp control=inspire:0.0'

#	./read_pci.scp

avview: $(OBJS) main.c
	$(CC) $(OBJS) $(TCLLIBS) $(TKLIBS) $(LIBS) main.c -o avview 

avview.static: $(OBJS) main.c
	$(CC) -static $(OBJS) $(TCLLIBS) $(LIBS) main.c -lm -dl -o avview.static
	strip avview.static
	
	
tarball:
	(cd .. ; tar cvf - avview/*.scp avview/*.[c,h] avview/Makefile avview/README | gzip - ) > avview.tgz

clean:
	rm -f *.o *.bck *.bak core
