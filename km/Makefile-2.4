KM_FLAGS=
CFLAGS = -march=i586 -O2 -Wall -Wstrict-prototypes -pipe -fno-strict-aliasing -fno-common $(KM_FLAGS)
LDFLAGS = -s -N
CC=gcc

TOPDIR=/usr/src/linux

LINUXINCLUDES=$(TOPDIR)/include

KM_OBJS=km.o km_v4l.o km_memory.o radeon.o mach64.o rage128.o

KM_API_OBJS=km_api.o km_api_data.o km_memory.o

all: km_drv.o km_api_drv.o

km.o:	km.c 
	$(CC) $(CFLAGS) -DMODULE -D__KERNEL__ -DLINUX -I$(LINUXINCLUDES) -c km.c

km_api.o:	km_api.c 
	$(CC) $(CFLAGS) -DMODULE -D__KERNEL__  -DEXPORT_SYMTAB -DLINUX -I$(LINUXINCLUDES) -c km_api.c

km_api_data.o:	km_api_data.c 
	$(CC) $(CFLAGS) -DMODULE -D__KERNEL__  -DEXPORT_SYMTAB -DLINUX -I$(LINUXINCLUDES) -c km_api_data.c

km_drv.o: $(KM_OBJS)
	ld -r $(KM_OBJS) -o km_drv.o

km_api_drv.o: $(KM_API_OBJS)
	ld -r $(KM_API_OBJS) -o km_api_drv.o

km_v4l.o:	km_v4l.c
	$(CC) $(CFLAGS) -DMODULE -D__KERNEL__ -DLINUX -I$(LINUXINCLUDES) -c km_v4l.c

km_memory.o:	km_memory.c
	$(CC) $(CFLAGS) -DMODULE -D__KERNEL__ -DLINUX -I$(LINUXINCLUDES) -c km_memory.c

radeon.o:	radeon.c
	$(CC) $(CFLAGS) -DMODULE -D__KERNEL__ -DLINUX -I$(LINUXINCLUDES) -c radeon.c

mach64.o:	mach64.c
	$(CC) $(CFLAGS) -DMODULE -D__KERNEL__ -DLINUX -I$(LINUXINCLUDES) -c mach64.c

rage128.o:	rage128.c
	$(CC) $(CFLAGS) -DMODULE -D__KERNEL__ -DLINUX -I$(LINUXINCLUDES) -c rage128.c

test: km_drv.o km_api_drv.o
	@echo "** Syncing disks"
	sync
	@echo "** Trying to make sure v4l module is loaded"
	modprobe videodev || true
	@echo "** Now loading km modules"
	insmod ./km_api_drv.o
	insmod ./km_drv.o km_debug=0 km_buffers=5

test2: test2.c
	$(CC) $(CFLAGS) test2.c -o test2

install:
	mkdir -p /lib/modules/`uname -r`/misc/
	install km_api_drv.o /lib/modules/`uname -r`/misc/
	install km_drv.o /lib/modules/`uname -r`/misc/

tarball:
	(cd .. ; tar cvf - km/*.[c,h] km/Makefile km/README km/km.*.txt km/test*.tcl | gzip - ) > km.tgz

remove:
	rmmod km_drv || true
	rmmod km_api_drv || true

clean:
	rm -f *.o *.bck *.bak core

