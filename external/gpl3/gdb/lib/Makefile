#	$NetBSD: Makefile,v 1.3 2012/11/03 14:38:17 skrll Exp $

.include "../Makefile.inc"

SUBDIR=		libbfd libiberty libopcodes libgdb libdecnumber
.if ${USE_LIBEDIT} == no || ${USE_TUI} == yes
SUBDIR+=	libreadline
.endif

.if ${MACHINE_ARCH} == powerpc || ${MACHINE_ARCH} == powerpcle || \
    ${MACHINE_ARCH} == powerpc64
SUBDIR+= libsim
.endif


.include <bsd.subdir.mk>
