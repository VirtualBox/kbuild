# Copyright (c) 2006 knut st. osmundsen <bird-kbuild-src@anduin.net>
# Distributed under the terms of the GNU General Public License v2
# $Header: $

inherit subversion

DESCRIPTION="kBuild"
HOMEPAGE="http://svn.netlabs.org/kbuild"
ESVN_REPO_URI="http://svn.netlabs.org/repos/kbuild/trunk"
ESVN_STORE_DIR="${DISTDIR}/svn-src"
ESVN_PROJECT="${PN/-svn}"

SLOT="0"
LICENSE="GPL-2"
KEYWORDS="amd64 ~ppc ~sparc x86"
IUSE=""

KBUILD_SRC_TREE="${ESVN_STORE_DIR}/kBuild/trunk"

src_compile() {
    ${KBUILD_SRC_TREE}/kBuild/env.sh \
        kmk NIX_INSTALL_DIR=/usr BUILD_TYPE=release -C ${KBUILD_SRC_TREE} \
            || die "kmk failed"
}

src_install () {
    ${KBUILD_SRC_TREE}/kBuild/env.sh \
        kmk NIX_INSTALL_DIR=/usr BUILD_TYPE=release -C ${KBUILD_SRC_TREE} PATH_INS=${D} install \
            || die "kmk install failed"
    strip ${D}/usr/bin/k* || die "strip failed"
}
