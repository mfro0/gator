CFLAGS = -m486 -O2 -Wall -Wstrict-prototypes -pipe -fno-strict-aliasing -fno-common
LDFLAGS = -s -N
CC=gcc

KM_OBJS=km.o km_v4l.o km_memory.o radeon.o

all: km_drv.o

km.o:	km.c 
	$(CC) $(CFLAGS) -DMODULE -D__KERNEL__ -DLINUX -I/usr/src/linux/include -c km.c

km_drv.o: $(KM_OBJS)
	ld -r $(KM_OBJS) -o km_drv.o

km_v4l.o:	km_v4l.c
	$(CC) $(CFLAGS) -DMODULE -D__KERNEL__ -DLINUX -I/usr/src/linux/include -c km_v4l.c

km_memory.o:	km_memory.c
	$(CC) $(CFLAGS) -DMODULE -D__KERNEL__ -DLINUX -I/usr/src/linux/include -c km_memory.c

radeon.o:	radeon.c
	$(CC) $(CFLAGS) -DMODULE -D__KERNEL__ -DLINUX -I/usr/src/linux/include -c radeon.c

test: km_drv.o
	sync
	insmod ./km_drv.o 

install:
	mkdir -p /lib/modules/`uname -r`/misc/
	install km_drv.o /lib/modules/`uname -r`/misc/

tarball:
	(cd .. ; tar cvf - km/*.[c,h] km/Makefile km/README | gzip - ) > km.tgz

clean:
	rm -f *.o *.bck *.bak core

