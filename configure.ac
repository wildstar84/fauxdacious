dnl ***
dnl *** Process this file with autoconf to produce a configure script.
dnl ***

dnl Initialize
dnl ==========

AC_PREREQ([2.59])
AC_INIT([fauxdacious], [4.5-final])
AC_COPYRIGHT([Copyright (C) 2001-2025 Jim Turner, Audacious developers and others])

AC_DEFINE_UNQUOTED(PACKAGE, "$PACKAGE_NAME", [Name of package])
AC_DEFINE_UNQUOTED(VERSION, "$PACKAGE_VERSION", [Version number of package])

COPYRIGHT="Copyright (C) 2001-2025 Jim Turner, Audacious developers and others"
AC_DEFINE_UNQUOTED(COPYRIGHT, "$COPYRIGHT", [Copyright])
AC_SUBST(COPYRIGHT)

AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

AUD_COMMON_PROGS

BUILDSYS_INIT
BUILDSYS_SHARED_LIB
BUILDSYS_PLUGIN

AC_DEFINE_UNQUOTED(PLUGIN_SUFFIX, "$PLUGIN_SUFFIX", [Suffix for plugins])

m4_ifdef([PKG_INSTALLDIR], [PKG_INSTALLDIR], AC_SUBST([pkgconfigdir], ${libdir}/pkgconfig))

dnl Headers and functions
dnl ===============================

AC_CHECK_FUNCS([sigwait])

dnl iconv
dnl =====

AM_ICONV
LIBS="$LIBS $LIBICONV"

dnl gettext
dnl =======
AM_GNU_GETTEXT([external])
AM_GNU_GETTEXT_VERSION(0.18.1)
if test "$MSGFMT" = ":" ; then
    AC_MSG_ERROR([msgfmt was not found; have you installed gettext?])
fi

LIBS="$LIBS $LIBINTL"

dnl Build stamp
dnl ===========
AC_ARG_WITH([buildstamp],
 AS_HELP_STRING([--with-buildstamp=XXX], [Set build stamp to XXX]),
 BUILDSTAMP="$withval", BUILDSTAMP="unknown build")

AC_DEFINE_UNQUOTED(BUILDSTAMP, "$BUILDSTAMP", [Build stamp])
AC_SUBST(BUILDSTAMP)

dnl D-Bus support
dnl =============

AC_ARG_ENABLE(dbus,
 AS_HELP_STRING(--disable-dbus, [Disable D-Bus support (default=enabled)]),
 USE_DBUS=$enableval, USE_DBUS=yes)

if test $USE_DBUS = yes ; then
    AC_CHECK_PROG(have_dbus, gdbus-codegen, yes, no)
    if test $have_dbus = no ; then
        AC_MSG_ERROR([D-Bus support unavailable; use --disable-dbus for a crippled build])
    fi
    AC_DEFINE(USE_DBUS, 1, [Define if D-Bus support enabled])
fi

AC_SUBST(USE_DBUS)

dnl SDL (needed for SDL audio and/or any Video play)!
dnl =========================

dnl (SDL2 NOW REQUIRED TO BUILD FAUXDACIOUS!) AC_ARG_ENABLE(sdl2,
dnl  AS_HELP_STRING(--disable-sdl2, [Disable SDL2 (Video) support (default=enabled)]),
dnl  USE_SDL2=$enableval, USE_SDL2=yes)
USE_SDL2=yes

have_sdl2=no
if test $USE_SDL2 = yes ; then
    PKG_CHECK_MODULES(SDL, sdl2 >= 2.0, [
        have_sdl2=yes
        AC_DEFINE(USE_SDL2, 1, [Defined if SDL2 support enabled])
    ], [
        AC_MSG_ERROR([SDL2 support REQUIRED, but unavailable due to missing dependency: sdl2 >= 2.0])
    ])
fi

dnl Valgrind analysis support
dnl =========================

AC_ARG_ENABLE(valgrind,
 AS_HELP_STRING(--enable-valgrind, [Valgrind analysis support (default=disabled)]),
 enable_valgrind=$enableval, enable_valgrind=no)

if test $enable_valgrind = yes ; then
    AC_DEFINE(VALGRIND_FRIENDLY, 1, [Define to allow Valgrind analysis])
    VALGRIND_FRIENDLY=1
else
    VALGRIND_FRIENDLY=0
fi

AC_SUBST([VALGRIND_FRIENDLY])

dnl Paths
dnl =====

plugindir="$libdir/fauxdacious"
AC_SUBST([plugindir])

localedir="$datarootdir/locale"
AC_SUBST([localedir])

dnl Reliably #include "config.h" (for large file support)
dnl =====================================================
CPPFLAGS="$CPPFLAGS -include config.h"

dnl Interface versions
dnl ==================
if test "x$USE_GTK3" = "xno"; then
    GTK_VSN="2"
elif test "x$USE_GTK3" = "x"; then
    GTK_VSN="2"
else
    GTK_VSN="3"
fi
if test "x$USE_QT5" = "xyes"; then
    QT_VSN="5"
else
    QT_VSN="6"
fi

### ---------------------------------------------------------------------------
dnl Output configuration files
dnl ==========================
define(GENERATED_FILES,
       fauxdacious.pc
       buildsys.mk
       extra.mk
       man/fauxdtool.1
       man/fauxdacious.1
       src/libfauxdcore/audio.h
       src/libfauxdcore/tinylock.h
       win32/override/README.txt)

[GENERATED_FILES]="m4_normalize(GENERATED_FILES)"
AC_SUBST([GENERATED_FILES])

AC_CONFIG_FILES(GENERATED_FILES)
AC_OUTPUT

### ---------------------------------------------------------------------------
dnl Print out the results
dnl =====================
echo ""
echo "Configuration:"
echo ""
echo "  Install path:                           $prefix"
echo ""
echo "  D-Bus support:                          $USE_DBUS"
echo "  GTK support:                            $USE_GTK"
if test "x$USE_GTK" = "xyes" ; then
    echo "  GTK Interface (release#)                $GTK_VSN"
fi
echo "  Qt support:                             $USE_QT"
if test "x$USE_QT" = "xyes" ; then
    echo "  Qt Interface (release#)                 $QT_VSN"
fi
echo "  SDL2 (Video) support:                   $have_sdl2"
echo "  Valgrind analysis support:              $enable_valgrind"
echo ""
