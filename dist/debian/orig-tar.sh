#!/bin/sh -e

SVNROOT=$1
VERSION=$2
REVISION=$3

DIR=kbuild-$REVISION
TAR=../kbuild_$VERSION.orig.tar.gz

svn co -r $REVISION $SVNROOT $DIR
tar -c -z  --exclude '*/kBuild/bin*' --exclude '*/out/*' --exclude '*/.svn*' -f $TAR $DIR
rm -rf $DIR

# move to directory 'tarballs'
if [ -r .svn/deb-layout ]; then
  . .svn/deb-layout
  mv $TAR $origDir
  echo "moved $TAR to $origDir"
fi

