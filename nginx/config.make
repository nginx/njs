cat << END                                            >> $NGX_MAKEFILE

$NJS_BUILD_DIR/libnjs.a: $NGX_MAKEFILE
	cd $ngx_addon_dir/.. \\
	&& CFLAGS="\$(CFLAGS)" CC="\$(CC)" ./configure --no-openssl \\
		--no-libxml2 --no-zlib --no-pcre --no-quickjs \\
		--no-root-makefile --build-dir="$NJS_BUILD_DIR" \\
		--ld-opt="$NGX_LD_OPT" \\
	&& \$(MAKE) -f $NJS_BUILD_DIR/Makefile libnjs

$NJS_BUILD_DIR/libqjs.a: $NGX_MAKEFILE
	cd $ngx_addon_dir/.. \\
	&& CFLAGS="\$(CFLAGS)" CC="\$(CC)" ./configure --no-openssl \\
		--no-libxml2 --no-zlib --no-pcre --with-quickjs \\
		--no-root-makefile --build-dir="$NJS_BUILD_DIR" \\
		--quickjs-inc="$NJS_QUICKJS_INC" \\
		--quickjs-lib="$NJS_QUICKJS_LIB" --ld-opt="$NGX_LD_OPT" \\
	&& \$(MAKE) -f $NJS_BUILD_DIR/Makefile libnjs libqjs

END
