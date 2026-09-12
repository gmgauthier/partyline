#!/bin/sh
# Build release artifacts: tarball, .deb, optional AppImage.
# Usage: ./scripts/release.sh [tarball|deb|appimage|all]

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

VERSION=$(sed -n "s/^  version: '\\(.*\\)',/\\1/p" meson.build | head -1)
DISTDIR="${ROOT}/dist"
JOB=${1:-all}

mkdir -p "$DISTDIR"

need_build() {
  if [ ! -f "${ROOT}/build/build.ninja" ]; then
    meson setup "${ROOT}/build" "$ROOT"
  fi
}

do_tarball() {
  need_build
  meson dist -C "${ROOT}/build" --no-tests --allow-dirty
  src="${ROOT}/build/meson-dist/partyline-${VERSION}.tar.xz"
  if [ -f "$src" ]; then
    cp -f "$src" "$DISTDIR/"
    echo "tarball: ${DISTDIR}/partyline-${VERSION}.tar.xz"
  else
    echo "meson dist did not produce partyline-${VERSION}.tar.xz" >&2
    ls -la "${ROOT}/build/meson-dist" >&2 || true
    exit 1
  fi
}

do_deb() {
  dpkg-buildpackage -us -uc -b --no-sign
  mkdir -p "$DISTDIR"
  for f in "${ROOT}/../partyline_${VERSION}"-*.deb \
           "${ROOT}/../partyline-dbgsym_${VERSION}"-*.deb; do
    [ -e "$f" ] || continue
    mv -f "$f" "$DISTDIR/"
    echo "deb: $DISTDIR/$(basename "$f")"
  done
  for f in "${ROOT}/../partyline_${VERSION}"-*.buildinfo \
           "${ROOT}/../partyline_${VERSION}"-*.changes; do
    [ -e "$f" ] || continue
    mv -f "$f" "$DISTDIR/"
  done
}

do_appimage() {
  if ! command -v linuxdeploy >/dev/null 2>&1; then
    echo "linuxdeploy not on PATH; skip AppImage." >&2
    echo "See INSTALL.md §3." >&2
    return 0
  fi
  APPDIR="${ROOT}/build/AppDir"
  rm -rf "$APPDIR"
  meson setup "${ROOT}/build-appimage" "$ROOT" --prefix=/usr
  meson compile -C "${ROOT}/build-appimage"
  DESTDIR="$APPDIR" meson install -C "${ROOT}/build-appimage"

  GIOMOD=$(pkg-config --variable=giomoduledir gio-2.0 2>/dev/null || true)
  GIOMOD="${GIOMOD:-/usr/lib/x86_64-linux-gnu/gio/modules}"
  if [ -f "${GIOMOD}/libgiognutls.so" ]; then
    mkdir -p "${APPDIR}/usr/lib/x86_64-linux-gnu/gio/modules"
    cp -a "${GIOMOD}/libgiognutls.so" "${APPDIR}/usr/lib/x86_64-linux-gnu/gio/modules/"
    mkdir -p "${APPDIR}/apprun-hooks"
    printf '%s\n' \
      'export GIO_MODULE_DIR="${APPDIR}/usr/lib/x86_64-linux-gnu/gio/modules"' \
      > "${APPDIR}/apprun-hooks/gio-modules.sh"
  fi

  export LINUXDEPLOY_OUTPUT_VERSION="$VERSION"
  export APPIMAGE_EXTRACT_AND_RUN=1
  PLUGIN_ARGS=""
  if command -v linuxdeploy-plugin-gtk >/dev/null 2>&1 || \
     [ -x "${ROOT}/scripts/linuxdeploy-plugin-gtk.sh" ]; then
    PLUGIN_ARGS="--plugin gtk"
    if [ -x "${ROOT}/scripts/linuxdeploy-plugin-gtk.sh" ]; then
      export PATH="${ROOT}/scripts:${PATH}"
    fi
  fi
  EXTRA_LIB=""
  if [ -f "${GIOMOD}/libgiognutls.so" ]; then
    EXTRA_LIB="--library ${GIOMOD}/libgiognutls.so"
  fi
  # shellcheck disable=SC2086
  linuxdeploy --appdir "$APPDIR" \
    --executable "${APPDIR}/usr/bin/partyline" \
    --desktop-file "${APPDIR}/usr/share/applications/partyline.desktop" \
    --icon-file "${APPDIR}/usr/share/icons/hicolor/scalable/apps/partyline.svg" \
    $PLUGIN_ARGS \
    $EXTRA_LIB \
    --output appimage
  mkdir -p "$DISTDIR"
  for f in "${ROOT}/Partyline-${VERSION}"-*.AppImage \
           "${ROOT}/partyline-${VERSION}"-*.AppImage \
           "${ROOT}/build/Partyline-${VERSION}"-*.AppImage \
           "${ROOT}"/*.AppImage; do
    [ -e "$f" ] || continue
    mv -f "$f" "$DISTDIR/"
  done
  echo "appimage: $(ls -1 "$DISTDIR"/*.AppImage 2>/dev/null | tail -1)"
}

case "$JOB" in
  tarball) do_tarball ;;
  deb)     do_deb ;;
  appimage) do_appimage ;;
  all)
    do_tarball
    do_deb
    do_appimage
    ;;
  *)
    echo "Usage: $0 [tarball|deb|appimage|all]" >&2
    exit 2
    ;;
esac
