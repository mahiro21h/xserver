
. .github/scripts/conf.sh

pkg_dir() {
    echo -n "${X11_DEPS_DIR}/$1"
}

clone_source() {
    local pkgname="$1"
    local url="$2"
    local ref="$3"
    local commit="$4"

    local pkgdir=$(pkg_dir $pkgname)

    echo "clone: pkgdir=$pkgdir"
    pwd
    ls -la $pkgname || true

    if [ ! -f $pkgdir/.git/config ]; then
        echo "need to clone $pkgname into $pkgdir"
        if [ "$commit" ]; then
            git clone $url $pkgdir --branch=$ref
        else
            git clone $url $pkgdir --branch=$ref --depth 1
        fi
    else
        echo "already cloned $pkgname -- $pkgdir"
    fi

    if [ "$commit" ]; then
        ( cd $pkgdir && git checkout -f "$commit" )
    fi
}

build_meson() {
    local pkgname="$1"
    local url="$2"
    local ref="$3"
    local commit="$4"
    local pkgdir=$(pkg_dir $pkgname)
    shift
    shift
    shift
    shift || true
    if [ -f $X11_PREFIX/$pkgname.DONE ]; then
        echo "package $pkgname already built"
    else
        clone_source "$pkgname" "$url" "$ref" "$commit"
        (
            cd $pkgdir
            meson "$@" build -Dprefix=$X11_PREFIX
            ninja -j${FDO_CI_CONCURRENT:-4} -C build install
        )
        touch $X11_PREFIX/$pkgname.DONE
    fi
}

build_ac() {
    local pkgname="$1"
    local url="$2"
    local ref="$3"
    local commit="$4"
    local pkgdir=$(pkg_dir $pkgname)
    shift
    shift
    shift
    shift || true
    if [ -f $X11_PREFIX/$pkgname.DONE ]; then
        echo "package $pkgname already built"
    else
        clone_source "$pkgname" "$url" "$ref" "$commit"
        (
            cd $pkgdir
            ./autogen.sh --prefix=$X11_PREFIX
            make -j${FDO_CI_CONCURRENT:-4} install
        )
        touch $X11_PREFIX/$pkgname.DONE
    fi
}

build_drv_ac() {
    local pkgname="$1"
    local url="$2"
    local ref="$3"
    local commit="$4"
    local pkgdir=$(pkg_dir $pkgname)
    shift
    shift
    shift
    shift || true
    clone_source "$pkgname" "$url" "$ref" "$commit"
    (
        cd $pkgdir
        ./autogen.sh # --prefix=$X11_PREFIX
        make -j${FDO_CI_CONCURRENT:-4} # install
    )
}

build_ac_xts() {
    local pkgname="$1"
    local url="$2"
    local ref="$3"
    local commit="$4"
    local pkgdir=$(pkg_dir $pkgname)
    shift
    shift
    shift
    shift || true
    if [ -f $X11_PREFIX/$pkgname.DONE ]; then
        echo "package $pkgname already built"
    else
        clone_source "$pkgname" "$url" "$ref" "$commit"
        (
            cd $pkgdir
            CFLAGS='-fcommon'
            ./autogen.sh --prefix=$X11_PREFIX CFLAGS="$CFLAGS"
            make -j${FDO_CI_CONCURRENT:-4} install
        )
        touch $X11_PREFIX/$pkgname.DONE
    fi
}
