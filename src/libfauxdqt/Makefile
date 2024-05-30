SHARED_LIB = ${LIB_PREFIX}fauxdqt${LIB_SUFFIX}
LIB_MAJOR = 3
LIB_MINOR = 0

SRCS = about-qt.cc \
       art-qt.cc \
       audqt.cc \
       colorbutton.cc \
       dark-theme.cc \
       dock.cc \
       eq-preset-qt.cc \
       equalizer-qt.cc \
       file-entry.cc \
       fileopener.cc \
       font-entry.cc \
       images.cc \
       infopopup-qt.cc \
       infowin-qt.cc \
       info_bar.cc \
       info-widget.cc \
       log-inspector.cc \
       menu-qt.cc \
       playlist-management.cc \
       plugin-menu-qt.cc \
       prefs-builder.cc \
       prefs-plugin.cc \
       prefs-widget-qt.cc \
       prefs-window-qt.cc \
       prefs-pluginlist-model.cc \
       queue-manager-qt.cc \
       song-window-qt.cc \
       search-select-qt.cc \
       url-opener-qt.cc \
       util-qt.cc \
       treeview.cc \
       volumebutton.cc

INCLUDES = colorbutton.h \
           dock.h \
           export.h \
           iface.h \
           info_bar.h \
           info-widget.h \
           libfauxdqt.h \
           menu.h \
           treeview.h

CLEAN = images.cc

include ../../buildsys.mk
include ../../extra.mk

includesubdir = libfauxdqt

LD = ${CXX}

CPPFLAGS := -I.. -I../.. \
            ${CPPFLAGS} \
            ${QT_CFLAGS} \
            ${SDL_CFLAGS} \
            -DLIBAUDQT_BUILD

CFLAGS += ${LIB_CFLAGS}

LIB_LDFLAGS := -L../libfauxdcore $(LIB_LDFLAGS)

LIBS := -lfauxdcore \
        ${LIBS} ${SDL_LIBS} -lm \
        ${QT_LIBS}

images.cc: images.qrc
	${QT_BINPATH}/rcc images.qrc -o images.cc

