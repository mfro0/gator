
bin_PROGRAMS = avview_shell start_avview


avview_shell_SOURCES = xv.c frequencies.c xmisc.c string_cache.c v4l.c formats.c ffmpeg.c \
	alsa.c packet_stream.c linux.c main.c alsa.h config.h ffmpeg.h formats.h \
	frequencies.h global.h linux.h packet_stream.h string_cache.h v4l.h  \
	xmisc.h xv.h

scpdir = @datadir@/AVview
scp_DATA = alsa.scp bindings.scp ffmpeg.scp themes.scp video_device.scp \
	alsa_mixer.scp channel_scan.scp  km.scp tree.scp xv.scp	\
	avview.scp diag.scp setup.scp v4l.scp avview.modmap

#
# Well, it doesn't exist !
#
nodist_start_avview_SOURCES = start_avview.c

avview_shell_LDADD = @X_PRE_LIBS@ @X_LIBS@ @X_EXTRA_LIBS@
avview_shell_CFLAGS = -DAVVIEW_VERSION=\"@PACKAGE_VERSION@\"

start_avview$(EXEEXT): start_avview.in
	echo '#!$(bindir)/avview_shell' > start_avview
	echo '' >> start_avview
	echo 'global avview_version ; set avview_version "@PACKAGE_VERSION@" ' >> start_avview
	echo 'global avview_directory ; set avview_directory "${datadir}/AVview" ' >> start_avview
	cat start_avview.in >> start_avview
	chmod +x start_avview
