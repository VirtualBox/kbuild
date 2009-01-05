# Copyright 1999-2008 Gentoo Foundation
# Distributed under the terms of the GNU General Public License v2
# $Header: /var/cvsroot/gentoo-x86/dev-util/kbuild/kbuild-0.1.4.ebuild,v 1.4 2008/10/28 18:11:02 jokey Exp $

EAPI=1

WANT_AUTOMAKE=1.9

inherit eutils autotools

MY_P=kBuild-${PV}-src
DESCRIPTION="A makefile framework for writing simple makefiles for complex tasks"
HOMEPAGE="http://svn.netlabs.org/kbuild/wiki"
SRC_URI="ftp://ftp.netlabs.org/pub/kbuild/${MY_P}.tar.gz"

LICENSE="GPL-2 GPL-3 FDL-1.3"
SLOT="0"
KEYWORDS="amd64 x86"
IUSE=""

DEPEND="sys-devel/bison
	sys-devel/flex
	sys-devel/gettext"

S=${WORKDIR}/${MY_P/-src}

src_unpack() {
		unpack ${A}
		cd "${S}"
}

src_compile() {
		kBuild/env.sh --full \
		make -f bootstrap.gmk NIX_INSTALL_DIR=/usr || die "bootstrap failed"
}

src_install() {
		kBuild/env.sh --full-with-bin kmk \
		NIX_INSTALL_DIR=/usr \
		PATH_INS="${D}" \
		install || die "install failed"
}
