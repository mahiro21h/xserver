#!/bin/bash

set -e

. .github/scripts/util.sh

mkdir -p $X11_BUILD_DIR
cd $X11_BUILD_DIR

build_meson   rendercheck       https://gitlab.freedesktop.org/xorg/test/rendercheck     rendercheck-1.6
if [ "$X11_OS" = "Linux" ]; then
build_meson   drm               https://gitlab.freedesktop.org/mesa/drm                  libdrm-2.4.121   "" \
    -Domap=enabled
fi
build_meson   libxcvt           https://gitlab.freedesktop.org/xorg/lib/libxcvt          libxcvt-0.1.0
build_ac      xorgproto         https://gitlab.freedesktop.org/xorg/proto/xorgproto      xorgproto-2024.1
if [ "$X11_OS" = "Darwin" ]; then
build_ac      xset              https://gitlab.freedesktop.org/xorg/app/xset             xset-1.2.5
fi
build_ac_xts  xts               https://gitlab.freedesktop.org/xorg/test/xts             master     aae51229af810efba24412511f60602fab53eded

clone_source piglit             https://gitlab.freedesktop.org/mesa/piglit               main       265896c86f90cb72e8f218ba6a3617fca8b9a1e3

echo '[xts]' > "$(pkg_dir piglit)/piglit.conf"
echo "path=$X11_BUILD_DIR/xts" >> "$(pkg_dir piglit)/piglit.conf"
