CFLAGS = -m486 -O2 -Wall -Wstrict-prototypes -pipe -fno-strict-aliasing -fno-common
LDFLAGS = -s -N
CC=gcc

TOPDIR=/usr/src/linux

LINUXINCLUDES=$(TOPDIR)/include

KM_OBJS=km.o km_v4l.o km_memory.o radeon.o mach64.o rage128.o

KM_API_OBJS=km_api.o km_api_data.o

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

test: km_drv.o km_api.o
	sync
	insmod ./km_api_drv.o
	insmod ./km_drv.o km_debug=0 

install:
	mkdir -p /lib/modules/`uname -r`/misc/
	install km_api.o /lib/modules/`uname -r`/misc/
	install km_drv.o /lib/modules/`uname -r`/misc/

tarball:
	(cd .. ; tar cvf - km/*.[c,h] km/Makefile km/README | gzip - ) > km.tgz

remove:
	rmmod km_drv
	rmmod km_api_drv

clean:
	rm -f *.o *.bck *.bak core

