cat << END                                            >> $NGX_MAKEFILE

$ngx_addon_dir/../build/libnjs.a:
	cd $ngx_addon_dir/.. \\
	&& if [ -f build/Makefile ]; then \$(MAKE) clean; fi \\
	&& CFLAGS="\$(CFLAGS)" CC="\$(CC)" ./configure \\
	&& \$(MAKE)

END
