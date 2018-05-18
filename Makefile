SUBDIRS = src man images po

DISTCLEAN = ${GENERATED_FILES} config.h config.log config.status

DATA = AUTHORS \
       COPYING \
       images/about-logo.png \
       images/appearance.png \
       images/audio.png \
       images/connectivity.png \
       images/playlist.png \
       images/info.png \
       images/plugins.png \
       images/advanced.png
 
include buildsys.mk
include extra.mk

install-extra:
	for i in fauxdacious.pc; do \
	        ${INSTALL_STATUS}; \
		if ${MKDIR_P} ${DESTDIR}${pkgconfigdir} && ${INSTALL} -m 644 $$i ${DESTDIR}${pkgconfigdir}/$$i; then \
			${INSTALL_OK}; \
		else \
			${INSTALL_FAILED}; \
		fi; \
	done
	for i in fauxdacious.desktop; do \
		${INSTALL_STATUS}; \
		if ${MKDIR_P} ${DESTDIR}${datarootdir}/applications && ${INSTALL} -m 644 $$i ${DESTDIR}${datarootdir}/applications/$$i; then \
			${INSTALL_OK}; \
		else \
			${INSTALL_FAILED}; \
		fi \
	done
	if [ -f ${DESTDIR}${datadir}/fauxdacious/Skins/Default/balance.png ]; then \
		rm -f ${DESTDIR}${datadir}/fauxdacious/Skins/Default/balance.png; \
	fi

uninstall-extra:
	for i in fauxdacious.pc; do \
		if [ -f ${DESTDIR}${pkgconfigdir}/$$i ]; then \
			if rm -f ${DESTDIR}${pkgconfigdir}/$$i; then \
				${DELETE_OK}; \
			else \
				${DELETE_FAILED}; \
			fi \
		fi; \
	done
	for i in fauxdacious.desktop; do \
		if test -f ${DESTDIR}${datarootdir}/applications/$$i; then \
			if rm -f ${DESTDIR}${datarootdir}/applications/$$i; then \
				${DELETE_OK}; \
			else \
				${DELETE_FAILED}; \
			fi \
		fi \
	done
