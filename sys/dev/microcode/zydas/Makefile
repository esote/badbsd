# $OpenBSD: Makefile,v 1.16 2017/01/23 01:00:58 jsg Exp $

NOPROG=
NOMAN=
CC=${HOSTCC}

# USB capable systems only
.if (${MACHINE} == "amd64" || ${MACHINE} == "i386"

FIRM=	zd1211 zd1211b

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
	    ${.CURDIR}/zd1211-license ${DESTDIR}/etc/firmware
.endif

.include <bsd.prog.mk>
