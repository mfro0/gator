CFLAGS = -m486 -O2 -Wall -Wstrict-prototypes -pipe -fno-strict-aliasing -fno-common
LDFLAGS = -s -N
CC=gcc

TOPDIR=/usr/src/linux

ATI_REMOTE_OBJS=km.o km_v4l.o km_memory.o radeon.o mach64.o rage128.o

ATI_REMOTE_API_OBJS=km_api.o

all: ati_remote.o

ati_remote.o:	ati_remote.c 
	$(CC) $(CFLAGS) -DMODULE -D__KERNEL__ -DLINUX -I/usr/src/linux/include -c ati_remote.c


test: ati_remote.o
	sync
	insmod ./ati_remote.o

install:
	mkdir -p /lib/modules/`uname -r`/misc/
	install ati_remote.o /lib/modules/`uname -r`/misc/

tarball:
	(cd .. ; tar cvf - ati_remote/*.[c,h] ati_remote/Makefile ati_remote/README | gzip - ) > ati_remote.tgz

remove:
	rmmod ati_remote

clean:
	rm -f *.o *.bck *.bak core

