If you are running a 2.4 kernel make sure you patch your kernel 
with the v4l patches from http://bytesex.org/patches/

To compile type make

If you want to compile for a different kernel version you can use
make KDIR=/usr/src/linux-2.x.xx 

To load the module for 2.4 kernels type
insmod ./genericv4l.o disableinterlace=1
To load the module for 2.6 kernels type
insmod ./genericv4l.ko disableinterlace=1
(disableinterlace will limit capture to a max of 720x240 for NTSC, 
but it looks better than 720x480 interlaced :)

then start xawtv or any other v4l program

For a list of module options type
modinfo genericv4l.o  (for 2.4 kernels)
or 
modinfo genericv4l.ko  (for 2.6 kernels)

IF THE MODULE FAILS TO LOAD and your running a 2.4 kernel
The first thing you should do is make sure your kernel is patched
with the proper v4l2 patch for your kernel. 

You can download patches here
http://bytesex.org/patches/
The patch for 2.4.25 would be
http://bytesex.org/patches/2.4.25-rc1-1/20_v4l2-2.4.25-rc1.diff.gz

So for the 2.4.25 kernel you would do this
cd /usr/src
wget http://bytesex.org/patches/2.4.25-rc1-1/20_v4l2-2.4.25-rc1.diff.gz
gunzip 20_v4l2-2.4.25-rc1.diff.gz
cd /usr/src/linux
#do a dry run patch just incase we got the wrong one
patch --dry-run -p1 <../20_v4l2-2.4.25-rc1.diff
#if everything looked good run it for real
patch -p1 <../20_v4l2-2.4.25-rc1.diff 

#then make your kernel
make ; make modules; make modules_install; make install
(or make bzImage or whatever you have to do :)

People running the 2.6.x kernels do not need to apply any patches.

If it fails for any other reason email me!
Please include the output of dmesg in the email

NOTE: if you load the module and Tuner is not found
(look in dmesg) you can try to specify the tuner with the tunertype parameter
If you have a SECAM tuner you MUST specify tunertype=2

If you are using GATOS as well, keep in mind that you cannot use
both at the same time (you CANNOT watch tv in overlay mode and use
my driver to capture video at the same time)
If you have trouble getting my driver to work please follow these steps.
1) boot up the computer
2) start X (if you want to use the console skip this)
3) load my module
4) start capturing
(Just make sure you dont use gatos between the time you load my module
and start capturing, since we both try to control the card :)

Eric
