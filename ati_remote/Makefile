
ifeq ($(KDIR),)
	export KDIR := /usr/src/linux
endif

ifeq ($(PWD),)
	export PWD := $(shell pwd)
endif

ifeq ($(KERNELRELEASE),)

    .DEFAULT:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) $(@)

else

    ifeq ($(PATCHLEVEL),6)
            obj-m  := ati_remote26.o
    endif

    ifeq ($(PATCHLEVEL),4)
            obj-m  := ati_remote.o
	    TOPDIR := $(KDIR)
            include $(TOPDIR)/Rules.make
    endif

endif

.PHONY: install test remove clean tarball

all: modules

install: modules
	echo "Installation rule TODO (is tricky)"

test: modules
	sync
	insmod $(PWD)/ati_remote26.ko || insmod $(PWD)/ati_remote.o

remove:
	sync
	rmmod ati_remote26 || rmmod ati_remote

clean:
	rm -f $(PWD)/*.o $(PWD)/*.ko $(PWD)/*.bck $(PWD)/*.bak $(PWD)/core
	rm -f $(PWD)/*.mod.c $(PWD)/.*.cmd $(PWD)/.*.flags

tarball:
	(cd $(PWD)/.. ; tar cvf - ati_remote/ati_remote.c ati_remote/ati_remote26.c ati_remote/Makefile ati_remote/README ati_remote/README_LIRC ati_remote/README.Makefile | gzip - ) > $(PWD)/ati_remote.tgz
