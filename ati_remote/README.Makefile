
Explanation of the tricks used in the Makefile:

First the KDIR and PWD variables are given default vaules if not
provided on the make commandline.

The first time through the file, KERNELRELEASE will be undefined --
it is defined by the toplevel makefile in the kernel source tree,
and we have not read that in yet.  We test for this, in order to
detect whether we are recursing yet or not.  If we are not recursing
already, then we install a .DEFAULT target that may get used to 
build the module using the kernel tree's scripts.

The .PHONY directive at the bottom then prevents any of the
pseudotargets listed there from calling the .DEFAULT rule.

The magic happens when "modules" (or any other target not
listed as .PHONY) is made.  This can happen either because you
ask for it explicitly, or because one of the other .PHONY targets
has asked for it.  These targets execute the .DEFAULT rule.

The .DEFAULT rule calls make again from the kernel source tree
and sets the SUBDIRS variable to the ati_remote directory.  That
causes the kernel build system to think that ati_remote is part
of the kernel source tree.  The kernel build system then enters
the ati_remote directory and uses our makefile a second time.

On the second time through, KERNELRELEASE is now defined, so
our makefile does not install a .DEFAULT rule -- we let the
kernel build system supply the .DEFAULT rule among others.

The kernel scripts will build any modules listed in the variable
obj-m.  We get our module built by putting ati_remote26.o or
ati_remote.o, depending on the kernel version as found in the
PATCHLEVEL variable, in obj-m.  In the case of 2.4 we also need
to set the TOPDIR variable (KDIR is mostly just TOPDIR renamed)
and we then need to include the Rules.make global rules file from
the kernel tree.  2.6 kbuild does that part automatically so we
don't need to do any such thing in the 2.6 case.  Note that in the
case of 2.6 a lot of files (.o, .mod.c, mod.o, .ko) are generated.
It is the .ko one that must be used for insmod.

2.6's kbuild really hasn't been thought out in this respect
however, so you get a few gratuitous things happenning like vmlinux
gets relinked.  There are probably ways to trick that but it
is harmless for now and the 2.6 kbuild system is a moving target,
so we don't want to get too far into it until it stabilizes.
Once that happens we may be able to figure out how to get the
modules object files list to just contain our module -- right
now if you allow the "modules_install" target to fall through the
.DEFAULT rule and into the kernel build system, you install all
the modules in the kernel tree, but not the one in ati_remote :-/

--
Brian S. Julin
