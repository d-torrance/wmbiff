#!/bin/sh
# $Id: prerelease.sh,v 1.2 2001/06/19 03:52:02 dwonis Exp $
# wmBiff prerelease script.  Used mainly by the upstream maintainer(s).
# Run this before making a new release of wmBiff
# You will need cvs2cl.

set -e -x

(
	cd wmbiff
	make indent
	make distclean
	make all
	make distclean
)

cat <<EOF > CHANGES
CVS Changelog.  Don't bother editing this file, because it will get overwritten
by maint/prerelease.sh .

The latest changelog entry is in a different format, because it's inserted by
CVS on commit.

EOF

echo "  $""Log""$" >> CHANGES
echo ""            >> CHANGES

cvs2cl --stdout --utc --day-of-week -t | sed -f maint/changelog.sed >> CHANGES

set +x

echo ""
echo ""
echo ""
echo ""
echo "Pre-release script successful!"
echo ""
echo "Don't forget to edit RELEASE-NOTES, and substitute today's date for"
echo "(unreleased), commit and tag the release, then add a new version line"
echo "and commit again."
echo ""
echo "Also, don't forget to update WMBIFF_VERSION in wmbiff/Makefile ."
echo ""
echo "The date for the release notes is:   `date -R`"
