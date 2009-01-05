# Copyright 1999-2008 Gentoo Foundation
# Distributed under the terms of the GNU General Public License v2
# $Header: /var/cvsroot/gentoo-x86/dev-util/kbuild/kbuild-0.1.4.ebuild,v 1.4 2008/10/28 18:11:02 jokey Exp $

EAPI=1

WANT_AUTOMAKE=1.9

inherit eutils autotools

MY_P=kBuild-${PV}-src
DESCRIPTION="A makefile framework for writing simple makefiles for complex tasks"
HOMEPAGE="http://svn.netlabs.org/kbuild/wiki"
SRC_URI="ftp://ftp.netlabs.org/incoming/kBuild-${PV}/${MY_P}.tar.gz"

LICENSE="GPL-2"
SLOT="0"
KEYWORDS="amd64 x86"
IUSE=""

DEPEND="dev-util/cvs
	sys-devel/gettext"

S=${WORKDIR}/${MY_P/-src}

src_unpack() {
		unpack ${A}
		cd "${S}"

		# Fix the unneeded inclusion of sys/acl.h (bug #236949)
		epatch "${FILESDIR}/${PN}-fix-acl-include.patch"

		rm -rf "${S}/kBuild/bin"
}

src_compile() {
		kBuild/env.sh --full \
		make -f bootstrap.gmk || die "bootstrap failed"
}

src_install() {
		kBuild/env.sh kmk \
		NIX_INSTALL_DIR=/usr \
		PATH_INS="${D}" \
		install || die "install failed"
}
