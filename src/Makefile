include ../extra.mk

SUBDIRS := fauxdacious libfauxdcore libfauxdtag libguess

ifeq ($(USE_GTK),yes)
SUBDIRS += libfauxdgui
endif

ifeq ($(USE_QT),yes)
SUBDIRS += libfauxdqt
endif

ifeq ($(USE_DBUS),yes)
SUBDIRS := dbus fauxdtool ${SUBDIRS}
endif

include ../buildsys.mk

fauxdacious: libfauxdcore
libfauxdcore: libguess
libfauxdtag: libfauxdcore

ifeq ($(USE_GTK),yes)
libfauxdgui: libfauxdcore
endif

ifeq ($(USE_QT),yes)
libfauxdqt: libfauxdcore
endif

ifeq ($(USE_DBUS),yes)
fauxdacious fauxdtool: dbus
endif
