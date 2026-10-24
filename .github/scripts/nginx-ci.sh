#!/bin/bash

set -euo pipefail

export DEB_BUILD_MAINT_OPTIONS=hardening=+all
export DEB_CFLAGS_MAINT_APPEND=-fPIC
export DEB_LDFLAGS_MAINT_APPEND=-Wl,--as-needed

prepare() {
    sudo apt-get update
    sudo apt-get install -y libssl-dev zlib1g-dev libpcre2-dev libxslt1-dev \
        libgeoip-dev libgd-dev libxml2-dev libedit-dev libperl-dev \
        libio-socket-ssl-perl \
        libtest-harness-perl libgd-perl expect meson ninja-build

    local cc_opt ld_opt
    cc_opt=$(dpkg-buildflags --get CFLAGS)
    ld_opt=$(dpkg-buildflags --get LDFLAGS)

    git clone https://github.com/nginx/nginx nginx-source
    git clone https://github.com/nginx/nginx-tests

    case "$CI_ENGINE" in
        njs) ;;
        quickjs)
            git clone https://github.com/bellard/quickjs
            CFLAGS="$cc_opt" LDFLAGS="$ld_opt" \
                make -C quickjs -j"$(nproc)" libquickjs.a
            ;;
        quickjs-ng)
            git clone --branch v0.16.2 --depth=1 \
                https://github.com/quickjs-ng/quickjs quickjs-ng
            CFLAGS="$cc_opt -fPIC" LDFLAGS="$ld_opt" \
                meson setup quickjs-ng/build quickjs-ng \
                    --prefix="$HOME/.local" --libdir=lib -Ddefault_library=static
            meson compile -C quickjs-ng/build
            meson install -C quickjs-ng/build
            ;;
        *) echo "Unknown engine: $CI_ENGINE" >&2; exit 1 ;;
    esac

    if [ "$CI_ASAN" = yes ]; then
        echo 'leak:ngx_event_process_init' > lsan_suppressions.txt
    fi
}

asan_environment() {
    case "$CI_ASAN" in
        yes)
            export ASAN_OPTIONS=detect_odr_violation=0:report_globals=0
            case "$CI_ASAN_FAST_UNWIND" in
                yes) ;;
                no) ASAN_OPTIONS+=:fast_unwind_on_malloc=0 ;;
                *)
                    echo "Unknown ASan fast-unwind configuration: $CI_ASAN_FAST_UNWIND" >&2
                    exit 1
                    ;;
            esac
            export LSAN_OPTIONS="suppressions=$PWD/lsan_suppressions.txt"
            ;;
        no) ;;
        *) echo "Unknown ASan configuration: $CI_ASAN" >&2; exit 1 ;;
    esac
}

test_engine() {
    local engine=$1

    case " $CI_TEST_ENGINES " in
        *" $engine "*) ;;
        *) echo "Test engine is not configured: $engine" >&2; exit 1 ;;
    esac

    asan_environment
    export TEST_NGINX_BINARY="$PWD/nginx-source/objs/nginx"
    export TEST_NGINX_VERBOSE=1
    if [ "$CI_LINKAGE" = dynamic ]; then
        TEST_NGINX_GLOBALS="load_module $PWD/nginx-source/objs/ngx_http_js_module.so;"
        TEST_NGINX_GLOBALS+=" load_module $PWD/nginx-source/objs/ngx_stream_js_module.so;"
        export TEST_NGINX_GLOBALS
    fi

    case "$engine" in
        njs)
            unset TEST_NGINX_GLOBALS_HTTP TEST_NGINX_GLOBALS_STREAM
            ;;
        qjs)
            export TEST_NGINX_GLOBALS_HTTP='js_engine qjs;'
            export TEST_NGINX_GLOBALS_STREAM='js_engine qjs;'
            ;;
        *) echo "Unknown test engine: $engine" >&2; exit 1 ;;
    esac

    ulimit -c unlimited
    export TMPDIR
    TMPDIR=$(mktemp -d)
    prove -v -j"$(nproc)" -Inginx-tests/lib --state=save nginx/t . \
        || prove -v -Inginx-tests/lib --state=failed
}

case "${1:-}" in
    prepare) prepare; exit ;;
    build) ;;
    test) test_engine "${2:?Missing test engine}"; exit ;;
    *) echo "Usage: $0 prepare|build|test <engine>" >&2; exit 1 ;;
esac

cc_opt=$(dpkg-buildflags --get CFLAGS)
ld_opt=$(dpkg-buildflags --get LDFLAGS)

case "$CI_ENGINE" in
    njs)
        export NJS_QUICKJS=NO
        ;;
    quickjs)
        cc_opt+=" -I$PWD/quickjs"
        ld_opt+=" -L$PWD/quickjs"
        ;;
    quickjs-ng)
        export PKG_CONFIG_PATH="$HOME/.local/lib/pkgconfig"
        ;;
    *) echo "Unknown engine: $CI_ENGINE" >&2; exit 1 ;;
esac

options=(
    --prefix=/tmp
    --with-http_realip_module
    --with-http_addition_module
    --with-http_sub_module
    --with-http_dav_module
    --with-http_flv_module
    --with-http_mp4_module
    --with-http_gunzip_module
    --with-http_gzip_static_module
    --with-http_random_index_module
    --with-http_secure_link_module
    --with-http_stub_status_module
    --with-mail
    --with-select_module
    --with-poll_module
    --with-http_auth_request_module
    --with-http_v2_module
    --with-http_slice_module
    --with-stream
    --with-stream_realip_module
    --with-threads
    --with-cpp_test_module
    --with-compat
    --with-http_degradation_module
    --with-http_xslt_module
    --with-http_image_filter_module
    --with-http_perl_module
    --with-http_geoip_module
    --with-stream_geoip_module
)

case "$CI_SSL" in
    yes)
        options+=(--with-http_ssl_module --with-mail_ssl_module
            --with-stream_ssl_module --with-stream_ssl_preread_module)
        ;;
    no) export NJS_OPENSSL=NO ;;
    *) echo "Unknown SSL configuration: $CI_SSL" >&2; exit 1 ;;
esac

case "$CI_LINKAGE" in
    static) options+=(--add-module=../nginx) ;;
    dynamic) options+=(--add-dynamic-module=../nginx) ;;
    *) echo "Unknown module linkage: $CI_LINKAGE" >&2; exit 1 ;;
esac

case "$CI_DEBUG" in
    yes) options+=(--with-debug) ;;
    no) ;;
    *) echo "Unknown debug configuration: $CI_DEBUG" >&2; exit 1 ;;
esac

case "$CI_DEBUG_MEMORY" in
    yes)
        cc_opt+=" -DNJS_DEBUG_MEMORY -DNGX_DEBUG_PALLOC -DNGX_DEBUG_MALLOC"
        ;;
    no) ;;
    *) echo "Unknown debug-memory configuration: $CI_DEBUG_MEMORY" >&2; exit 1 ;;
esac

case "$CI_ASAN" in
    yes)
        cc_opt+=" -fsanitize=address"
        ld_opt+=" -fsanitize=address"
        ;;
    no) ;;
    *) echo "Unknown ASan configuration: $CI_ASAN" >&2; exit 1 ;;
esac

asan_environment

(
    cd nginx-source
    auto/configure "${options[@]}" --with-cc-opt="$cc_opt" \
        --with-ld-opt="$ld_opt" || { cat objs/autoconf.err; exit 1; }
    make -j"$(nproc)" modules
    make -j"$(nproc)"
)

# Do not silently lose QuickJS coverage if library detection fails.
if [ "$CI_ENGINE" != njs ]; then
    grep -q '^#define NJS_HAVE_QUICKJS  1' \
        nginx-source/objs/njs/njs_auto_config.h
fi
