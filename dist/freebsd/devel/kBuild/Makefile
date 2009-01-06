# New ports collection makefile for:	kbuild
# Date created:		Mon Jul 28 14:34:33 BST 2008
# Whom:			Bruce Simpson
#
# $FreeBSD: ports/devel/kBuild/Makefile,v 1.4 2008/12/02 23:14:19 gahr Exp $
#

PORTNAME=	kBuild
PORTVERSION=	0.1.5
CATEGORIES=	devel
MASTER_SITES=	ftp://ftp.netlabs.org/pub/kbuild/
DISTNAME=	${PORTNAME}-${PORTVERSION}-src

MAINTAINER=	que_deseja@hotmail.com
COMMENT=	Makefile framework

USE_AUTOTOOLS=	automake:19 autoconf:261
USE_GMAKE=	yes

WRKSRC=		${WRKDIR}/${PORTNAME}-${PORTVERSION}

KBUILD_ARCH=	${MACHINE_ARCH:S/i386/x86/}
KBUILD_ENV=	ACLOCAL=${ACLOCAL} \
		AUTOMAKE=${AUTOMAKE} \
		AUTORECONF=${AUTORECONF}
KBUILD_BINS=	kDepIDB \
		kDepPre \
		kObjCache \
		kmk \
		kmk_append \
		kmk_ash \
		kmk_cat \
		kmk_chmod \
		kmk_cmp \
		kmk_cp \
		kmk_echo \
		kmk_expr \
		kmk_gmake \
		kmk_install \
		kmk_ln \
		kmk_md5sum \
		kmk_mkdir \
		kmk_mv \
		kmk_printf \
		kmk_redirect \
		kmk_rm \
		kmk_rmdir \
		kmk_sed \
		kmk_sleep \
		kmk_test \
		kmk_time
					

# Override autotools
run-autotools:
do-configure:

do-build:
	cd ${WRKSRC} && ${SETENV} ${KBUILD_ENV} ./kBuild/env.sh --full ${GMAKE} NIX_INSTALL_DIR=${PREFIX} -f bootstrap.gmk
	cd ${WRKSRC} && ./kBuild/env.sh --full-with-bin kmk NIX_INSTALL_DIR=${PREFIX} BUILD_TYPE=release -C ${WRKSRC}

do-install:
.for bin in ${KBUILD_BINS}
	${INSTALL_PROGRAM} ${WRKSRC}/out/freebsd.${KBUILD_ARCH}/release${PREFIX}/bin/${bin} ${PREFIX}/bin
.endfor
	${MKDIR) ${DATADIR}
	(cd ${WRKSRC}/out/freebsd.${KBUILD_ARCH}/release${PREFIX}/share/kBuild && ${COPYTREE_SHARE} \* ${DATADIR})

.include <bsd.port.mk>
