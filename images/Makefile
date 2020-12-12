include ../buildsys.mk

install-extra:
	${MKDIR_P} ${DESTDIR}${datarootdir}/icons/hicolor/48x48/apps
	${MKDIR_P} ${DESTDIR}${datarootdir}/icons/hicolor/24x24/apps
	${MKDIR_P} ${DESTDIR}${datarootdir}/icons/hicolor/scalable/apps
	${INSTALL} -m 644 fauxdacious.png "${DESTDIR}${datarootdir}/icons/hicolor/48x48/apps/fauxdacious.png"
	${INSTALL} -m 644 fauxdacious24.png "${DESTDIR}${datarootdir}/icons/hicolor/24x24/apps/fauxdacious.png"
	${INSTALL} -m 644 fauxdacious.svg "${DESTDIR}${datarootdir}/icons/hicolor/scalable/apps/fauxdacious.svg"

uninstall-extra:
	if test -f "${DESTDIR}${datarootdir}/icons/hicolor/48x48/apps/fauxdacious.png" ; then ${RM} "${DESTDIR}${datarootdir}/icons/hicolor/48x48/apps/fauxdacious.png" ; fi
	if test -f "${DESTDIR}${datarootdir}/icons/hicolor/24x24/apps/fauxdacious.png" ; then ${RM} "${DESTDIR}${datarootdir}/icons/hicolor/24x24/apps/fauxdacious.png" ; fi
	if test -f "${DESTDIR}${datarootdir}/icons/hicolor/scalable/apps/fauxdacious.svg" ; then ${RM} "${DESTDIR}${datarootdir}/icons/hicolor/scalable/apps/fauxdacious.svg" ; fi
