NJS_BUILD_TARGETS=libnjs
NJS_QUICKJS_OPT=--no-quickjs

if [ "$NJS_HAVE_QUICKJS" = YES ]; then
    NJS_BUILD_TARGETS="libnjs libqjs"
    NJS_QUICKJS_OPT="--with-quickjs --quickjs-inc=\"$NJS_QUICKJS_INC\" \
                     --quickjs-lib=\"$NJS_QUICKJS_LIB\""
fi

cat << END                                            >> $NGX_MAKEFILE

.PHONY: njs_build

njs_build: $NJS_BUILD_DIR/Makefile

$NJS_BUILD_DIR/Makefile: $NGX_MAKEFILE \\
    $ngx_addon_dir/../configure `echo $ngx_addon_dir/../auto/*`
	cd $ngx_addon_dir/.. \\
	&& CFLAGS="\$(CFLAGS)" CC="\$(CC)" ./configure --no-openssl \\
		--no-libxml2 --no-zlib --no-pcre --no-root-makefile \\
		--build-dir="$NJS_BUILD_DIR" --ld-opt="$NGX_LD_OPT" \\
		$NJS_QUICKJS_OPT

$NJS_ENGINE_DEP: njs_build

$NJS_BUILD_DIR/libnjs.a:
	cd $ngx_addon_dir/.. \\
	&& \$(MAKE) -f $NJS_BUILD_DIR/Makefile \\
		$NJS_BUILD_TARGETS

END

if [ "$NJS_HAVE_QUICKJS" = YES ]; then
    cat << END                                        >> $NGX_MAKEFILE

$NJS_BUILD_DIR/libqjs.a: $NJS_BUILD_DIR/libnjs.a

END
fi

cat << END                                            >> $NGX_MAKEFILE

$NJS_BUILD_DIR/njs_auto_config.h: $NJS_BUILD_DIR/libnjs.a

END
