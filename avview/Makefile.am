
#
# Technically start_avview is a script. However it can be considered
# as being compiled from start_avview.in and thus is similar to avview_shell
# from automake point of view
#
bin_PROGRAMS = avview_shell start_avview

INCLUDES = -I@TCL_INC_DIR@ -I@TK_INC_DIR@
avview_shell_SOURCES = xv.c frequencies.c xmisc.c string_cache.c v4l.c formats.c ffmpeg.c \
	alsa.c packet_stream.c linux.c main.c alsa.h config.h ffmpeg.h formats.h \
	frequencies.h global.h linux.h packet_stream.h string_cache.h v4l.h  \
	xmisc.h xv.h
avview_shell_LDFLAGS = @TCL_LIB_SPEC@ @TK_LIB_SPEC@ @TK_XLIBSW@

scpdir = @datadir@/AVview

scp_DATA = start_avview.desktop

nodist_scp_DATA = start_avview.desktop

dist_scp_DATA = alsa.scp bindings.scp ffmpeg.scp themes.scp video_device.scp \
	alsa_mixer.scp channel_scan.scp  km.scp tree.scp xv.scp	\
	avview.scp diag.scp setup.scp v4l.scp xmisc.scp avview.modmap \
	avview-16x16.ppm avview-control-16x16.ppm avview-48x48.png start_avview.desktop.in \
	README INSTALL

#
# Whoever said that the suffix is .c ?
#
nodist_start_avview_SOURCES = start_avview.c
dist_start_avview_SOURCES = start_avview.in

avview_shell_LDADD = @X_PRE_LIBS@ @X_LIBS@ @X_EXTRA_LIBS@ @FFMPEG_LIBS@
avview_shell_CFLAGS = -Wall -O2 -DAVVIEW_VERSION=\"@PACKAGE_VERSION@\" @FFMPEG_CFLAGS@

start_avview$(EXEEXT): start_avview.in configure.in Makefile.am
	echo '#!$(bindir)/avview_shell' > start_avview
	echo '' >> start_avview
	echo 'global avview_version ; set avview_version "@PACKAGE_VERSION@" ' >> start_avview
	echo 'global avview_directory ; set avview_directory "${datadir}/AVview" ' >> start_avview
	cat start_avview.in >> start_avview
	chmod +x start_avview

start_avview.desktop: start_avview.desktop.in configure.in Makefile.am
	cp start_avview.desktop.in start_avview.desktop
	echo 'Exec=$(bindir)/start_avview' >> start_avview.desktop
	echo 'Icon=$(datadir)/AVview/avview-48x48.png' >> start_avview.desktop

install-data-hook: reminders
	@echo ""
	@echo " ************************************************"
	@echo ""
	@echo "   To start AVview use $(bindir)/start_avview"
	@echo ""

install-exec-hook: reminders
	@echo ""
	@echo " ************************************************"
	@echo ""
	@echo "   To start AVview use $(bindir)/start_avview"
	@echo ""
	
all-local: reminders
	@echo ""
	@echo " *****************************************************"
	@echo ""
	@echo "  To try out AVview without installing it first run:"
	@echo ""
	@echo "       ./start_avview.no_install"
	@echo ""
	@echo "  This will always use code in current directory,"
	@echo "     even if another installed version of AVview exists"
	@echo ""

reminders: .reminder-install .reminder-readme

.reminder-install: INSTALL
	@echo ""
	@echo " *********************************************** "
	@echo "   Please do not forget to review INSTALL file "
	@echo " *********************************************** "
	@echo ""
	@touch .reminder-install
	
.reminder-readme: README
	@echo ""
	@echo " *********************************************** "
	@echo "   Please do not forget to review README file "
	@echo " *********************************************** "
	@echo ""
	@touch .reminder-readme
	
	
