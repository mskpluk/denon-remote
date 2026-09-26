#!/bin/sh
set -e

# Builds the .deb inside a pinned, rootless Podman container running Debian
# trixie's initial 13.0 release, instead of on the host (which tracks Debian
# testing/forky and is always newer). dpkg-shlibdeps bakes the actual glibc and
# Qt symbol versions used into the package's Depends: at build time, so building
# on forky records a floor no released trixie point release can satisfy.
# Building on 13.0 records the lowest floor the trixie series ever has, which
# every later point release satisfies too.
#
# Requires rootless Podman: `sudo apt install podman` once, interactively, in a
# real terminal -- not through this script. Everything after that runs
# unprivileged.
#
# Usage: scripts/build-deb-trixie.sh [output-dir]
#   output-dir defaults to ../denon-remote-deb-build, a sibling of the
#   repo, so build output never lands inside the git tree.
# Env: SKIP_VERIFY=1 skips the post-build install check on trixie (rolling) and
#   Ubuntu 26.04 LTS.

cd "$(dirname "$0")/.."

BASE_IMAGE="docker.io/library/debian:trixie-20250811"
OUT_DIR="${1:-../denon-remote-deb-build}"
CONTAINER_CMD=podman

if ! command -v "$CONTAINER_CMD" >/dev/null 2>&1; then
    echo "podman not found - install it first: sudo apt install podman" >&2
    exit 1
fi

DEB_VERSION=$(dpkg-parsechangelog -l debian/changelog -S Version)
echo "Building denon-remote $DEB_VERSION against $BASE_IMAGE ..."

STAGE_DIR=$(mktemp -d)
trap 'rm -rf "$STAGE_DIR"' EXIT

# git ls-files gives a clean tree: no local build artifacts leak into the
# container build.
git ls-files | while IFS= read -r f; do
    mkdir -p "$STAGE_DIR/$(dirname "$f")"
    cp "$f" "$STAGE_DIR/$f"
done

mkdir -p "$OUT_DIR"
OUT_DIR_ABS=$(cd "$OUT_DIR" && pwd)

"$CONTAINER_CMD" run --rm \
    -v "$STAGE_DIR:/src:ro" \
    -v "$OUT_DIR_ABS:/out" \
    "$BASE_IMAGE" \
    bash -c '
        set -e
        export DEBIAN_FRONTEND=noninteractive
        apt-get update -qq
        apt-get install -y -qq --no-install-recommends build-essential dpkg-dev
        apt-get build-dep -y -qq /src
        mkdir -p /build
        cp -r /src /build/src
        cd /build/src
        dpkg-buildpackage -us -uc -b
        cp /build/*.deb /out/
        cp /build/*.buildinfo /build/*.changes /out/ 2>/dev/null || true
    '

echo
echo "Built package(s):"
ls -la "$OUT_DIR"/*.deb

# Named from debian/changelog, not picked out of the directory: the output
# directory is not emptied between releases, and taking the first match from
# `ls` would verify a leftover package from an earlier build.
DEB_MAIN="$OUT_DIR/denon-remote_${DEB_VERSION}_amd64.deb"

if [ ! -f "$DEB_MAIN" ]; then
    echo "Package for version $DEB_VERSION not found at $DEB_MAIN" >&2
    exit 1
fi

if [ "$SKIP_VERIFY" = "1" ]; then
    exit 0
fi

echo
echo "Verifying $(basename "$DEB_MAIN") (version $DEB_VERSION from debian/changelog)"

# The program is a GUI with no headless mode, so the check is that the package
# installs with its dependencies resolved and the binary finds every library it
# links -- not that it runs. A container has no display and no receiver.
verify_on() {
    image="$1"
    label="$2"
    echo
    echo "=== Verifying install on $label ($image) ==="
    "$CONTAINER_CMD" run --rm \
        -v "$OUT_DIR_ABS:/pkgs:ro" \
        "$image" \
        bash -c "
            set -e
            export DEBIAN_FRONTEND=noninteractive
            apt-get update -qq
            apt-get install -y -qq '/pkgs/$(basename "$DEB_MAIN")' >/dev/null
            if ldd /usr/bin/denon-remote | grep -q 'not found'; then
                echo 'MISSING LIBS:'
                ldd /usr/bin/denon-remote | grep 'not found'
                exit 1
            fi
            echo 'OK: installs, all shared libraries resolved'
        "
}

verify_on "docker.io/library/debian:trixie" "Debian trixie (current point release)"
verify_on "docker.io/library/ubuntu:26.04" "Ubuntu 26.04 LTS"
