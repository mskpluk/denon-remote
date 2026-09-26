#!/bin/sh
set -e

# Packs the tracked tree at a tag into denon-remote-<version>.tar.gz. Taken
# with git archive from the tag rather than copied from the working tree, so
# nothing uncommitted or ignored (build trees, dist/) can end up in it.
#
# Usage: scripts/make-src-tarball.sh <tag> [output-dir]
#   tag is e.g. v0.2.0; output-dir defaults to ../denon-remote-deb-build.

cd "$(dirname "$0")/.."

TAG="$1"
OUT_DIR="${2:-../denon-remote-deb-build}"
if [ -z "$TAG" ]; then
    echo "Usage: $0 <tag> [output-dir]" >&2
    exit 1
fi

VERSION="${TAG#v}"
mkdir -p "$OUT_DIR"
git archive --format=tar.gz --prefix="denon-remote-$VERSION/" \
    -o "$OUT_DIR/denon-remote-$VERSION.tar.gz" "$TAG"
echo "Created $OUT_DIR/denon-remote-$VERSION.tar.gz"
