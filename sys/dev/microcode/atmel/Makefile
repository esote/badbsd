# $OpenBSD: Makefile,v 1.24 2017/01/23 01:00:58 jsg Exp $

NOPROG=
NOMAN=
CC=${HOSTCC}

# USB capable systems only
.if (${MACHINE} == "amd64" || ${MACHINE} == "i386"

FIRM= 	atu-intersil-int atu-intersil-ext \
	atu-rfmd-int atu-rfmd-ext \
	atu-rfmd2958-int atu-rfmd2958-ext \
	atu-rfmd2958smc-int atu-rfmd2958smc-ext \
	atu-at76c503-i3863-int atu-at76c503-i3863-ext \
	atu-at76c503-rfmd-acc-int atu-at76c503-rfmd-acc-ext \
	atu-at76c505-rfmd-int atu-at76c505-rfmd-ext

PROG=	build

CLEANFILES+= ${FIRM} ${PROG}

all: ${FIRM}

${FIRM}: build
	${.OBJDIR}/build

realinstall:

afterinstall:
	${INSTALL} -c -o ${BINOWN} -g ${BINGRP} -m 644 \
	    ${FIRM} ${DESTDIR}/etc/firmware
	${INSTALL} -c -o ${BINOWN} -g ${BINGRP} -m 644 \
	    ${.CURDIR}/atu-license ${DESTDIR}/etc/firmware
.endif

.include <bsd.prog.mk>
