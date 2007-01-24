# Copyright (c) 2006-2007 knut st. osmundsen <bird-kBuild-spam@anduin.net>
# Distributed under the terms of the GNU General Public License v2
# $Header: $

inherit eutils

DESCRIPTION="kBuild"
HOMEPAGE="http://svn.netlabs.org/kbuild"
#SRC_URI="http://ftp.netlabs.org/pub/kBuild/${P}.tar.gz"
SRC_URI="file:///home/bird/coding/kBuild/svn/${P}.tar.gz"

SLOT="0"
LICENSE="GPL-2"
KEYWORDS="amd64 ~ppc ~sparc x86"
IUSE=""

KBUILD_SRC_TREE="${WORKDIR}/trunk/"

src_compile() {
    append-cflags -g
    append-ldflags -g
    ${KBUILD_SRC_TREE}/kBuild/env.sh \
        kmk NIX_INSTALL_DIR=/usr BUILD_TYPE=release -C ${KBUILD_SRC_TREE} \
        || die "kmk failed"
}

src_install () {
    append-cflags -g
    append-ldflags -g
    ${KBUILD_SRC_TREE}/kBuild/env.sh \
        kmk NIX_INSTALL_DIR=/usr BUILD_TYPE=release -C ${KBUILD_SRC_TREE} PATH_INS=${D} install \
        || die "kmk install failed"
}

