
NXT_LIB =	nxt


$(NXT_BUILDDIR)/libnxt.a: \
	$(NXT_LIB)/nxt_auto_config.h \
	$(NXT_BUILDDIR)/nxt_diyfp.o \
	$(NXT_BUILDDIR)/nxt_dtoa.o \
	$(NXT_BUILDDIR)/nxt_strtod.o \
	$(NXT_BUILDDIR)/nxt_djb_hash.o \
	$(NXT_BUILDDIR)/nxt_utf8.o \
	$(NXT_BUILDDIR)/nxt_array.o \
	$(NXT_BUILDDIR)/nxt_queue.o \
	$(NXT_BUILDDIR)/nxt_rbtree.o \
	$(NXT_BUILDDIR)/nxt_lvlhsh.o \
	$(NXT_BUILDDIR)/nxt_random.o \
	$(NXT_BUILDDIR)/nxt_md5.o \
	$(NXT_BUILDDIR)/nxt_sha1.o \
	$(NXT_BUILDDIR)/nxt_sha2.o \
	$(NXT_BUILDDIR)/nxt_pcre.o \
	$(NXT_BUILDDIR)/nxt_malloc.o \
	$(NXT_BUILDDIR)/nxt_trace.o \
	$(NXT_BUILDDIR)/nxt_mem_cache_pool.o \

	ar -r -c $(NXT_BUILDDIR)/libnxt.a \
		$(NXT_BUILDDIR)/nxt_diyfp.o \
		$(NXT_BUILDDIR)/nxt_dtoa.o \
		$(NXT_BUILDDIR)/nxt_strtod.o \
		$(NXT_BUILDDIR)/nxt_djb_hash.o \
		$(NXT_BUILDDIR)/nxt_utf8.o \
		$(NXT_BUILDDIR)/nxt_array.o \
		$(NXT_BUILDDIR)/nxt_rbtree.o \
		$(NXT_BUILDDIR)/nxt_lvlhsh.o \
		$(NXT_BUILDDIR)/nxt_random.o \
		$(NXT_BUILDDIR)/nxt_md5.o \
		$(NXT_BUILDDIR)/nxt_sha1.o \
		$(NXT_BUILDDIR)/nxt_sha2.o \
		$(NXT_BUILDDIR)/nxt_pcre.o \
		$(NXT_BUILDDIR)/nxt_malloc.o \
		$(NXT_BUILDDIR)/nxt_trace.o \
		$(NXT_BUILDDIR)/nxt_mem_cache_pool.o \

$(NXT_BUILDDIR)/nxt_diyfp.o: \
	$(NXT_LIB)/nxt_types.h \
	$(NXT_LIB)/nxt_clang.h \
	$(NXT_LIB)/nxt_diyfp.h \
	$(NXT_LIB)/nxt_diyfp.c \

	$(NXT_CC) -c -o $(NXT_BUILDDIR)/nxt_diyfp.o $(NXT_CFLAGS) \
		-I$(NXT_LIB) \
		$(NXT_LIB)/nxt_diyfp.c

$(NXT_BUILDDIR)/nxt_dtoa.o: \
	$(NXT_LIB)/nxt_types.h \
	$(NXT_LIB)/nxt_clang.h \
	$(NXT_LIB)/nxt_dtoa.h \
	$(NXT_LIB)/nxt_dtoa.c \

	$(NXT_CC) -c -o $(NXT_BUILDDIR)/nxt_dtoa.o $(NXT_CFLAGS) \
		-I$(NXT_LIB) \
		$(NXT_LIB)/nxt_dtoa.c

$(NXT_BUILDDIR)/nxt_strtod.o: \
	$(NXT_LIB)/nxt_types.h \
	$(NXT_LIB)/nxt_clang.h \
	$(NXT_LIB)/nxt_strtod.h \
	$(NXT_LIB)/nxt_strtod.c \

	$(NXT_CC) -c -o $(NXT_BUILDDIR)/nxt_strtod.o $(NXT_CFLAGS) \
		-I$(NXT_LIB) \
		$(NXT_LIB)/nxt_strtod.c

$(NXT_BUILDDIR)/nxt_murmur_hash.o: \
	$(NXT_LIB)/nxt_types.h \
	$(NXT_LIB)/nxt_clang.h \
	$(NXT_LIB)/nxt_murmur_hash.h \
	$(NXT_LIB)/nxt_murmur_hash.c \

	$(NXT_CC) -c -o $(NXT_BUILDDIR)/nxt_murmur_hash.o $(NXT_CFLAGS) \
		-I$(NXT_LIB) \
		$(NXT_LIB)/nxt_murmur_hash.c

$(NXT_BUILDDIR)/nxt_djb_hash.o: \
	$(NXT_LIB)/nxt_types.h \
	$(NXT_LIB)/nxt_clang.h \
	$(NXT_LIB)/nxt_djb_hash.h \
	$(NXT_LIB)/nxt_djb_hash.c \

	$(NXT_CC) -c -o $(NXT_BUILDDIR)/nxt_djb_hash.o $(NXT_CFLAGS) \
		-I$(NXT_LIB) \
		$(NXT_LIB)/nxt_djb_hash.c

$(NXT_BUILDDIR)/nxt_utf8.o: \
	$(NXT_LIB)/nxt_types.h \
	$(NXT_LIB)/nxt_clang.h \
	$(NXT_LIB)/nxt_utf8.h \
	$(NXT_LIB)/nxt_unicode_lower_case.h \
	$(NXT_LIB)/nxt_unicode_upper_case.h \
	$(NXT_LIB)/nxt_utf8.c \

	$(NXT_CC) -c -o $(NXT_BUILDDIR)/nxt_utf8.o $(NXT_CFLAGS) \
		-I$(NXT_LIB) \
		$(NXT_LIB)/nxt_utf8.c

$(NXT_BUILDDIR)/nxt_array.o: \
	$(NXT_LIB)/nxt_types.h \
	$(NXT_LIB)/nxt_clang.h \
	$(NXT_LIB)/nxt_array.h \
	$(NXT_LIB)/nxt_array.c \

	$(NXT_CC) -c -o $(NXT_BUILDDIR)/nxt_array.o $(NXT_CFLAGS) \
		-I$(NXT_LIB) \
		$(NXT_LIB)/nxt_array.c

$(NXT_BUILDDIR)/nxt_queue.o: \
	$(NXT_LIB)/nxt_types.h \
	$(NXT_LIB)/nxt_clang.h \
	$(NXT_LIB)/nxt_queue.h \
	$(NXT_LIB)/nxt_queue.c \

	$(NXT_CC) -c -o $(NXT_BUILDDIR)/nxt_queue.o $(NXT_CFLAGS) \
		-I$(NXT_LIB) \
		$(NXT_LIB)/nxt_queue.c

$(NXT_BUILDDIR)/nxt_rbtree.o: \
	$(NXT_LIB)/nxt_types.h \
	$(NXT_LIB)/nxt_clang.h \
	$(NXT_LIB)/nxt_rbtree.h \
	$(NXT_LIB)/nxt_rbtree.c \

	$(NXT_CC) -c -o $(NXT_BUILDDIR)/nxt_rbtree.o $(NXT_CFLAGS) \
		-I$(NXT_LIB) \
		$(NXT_LIB)/nxt_rbtree.c

$(NXT_BUILDDIR)/nxt_lvlhsh.o: \
	$(NXT_LIB)/nxt_types.h \
	$(NXT_LIB)/nxt_clang.h \
	$(NXT_LIB)/nxt_string.h \
	$(NXT_LIB)/nxt_lvlhsh.h \
	$(NXT_LIB)/nxt_lvlhsh.c \

	$(NXT_CC) -c -o $(NXT_BUILDDIR)/nxt_lvlhsh.o $(NXT_CFLAGS) \
		-I$(NXT_LIB) \
		$(NXT_LIB)/nxt_lvlhsh.c

$(NXT_BUILDDIR)/nxt_random.o: \
	$(NXT_LIB)/nxt_types.h \
	$(NXT_LIB)/nxt_clang.h \
	$(NXT_LIB)/nxt_random.h \
	$(NXT_LIB)/nxt_random.c \

	$(NXT_CC) -c -o $(NXT_BUILDDIR)/nxt_random.o $(NXT_CFLAGS) \
		-I$(NXT_LIB) \
		$(NXT_LIB)/nxt_random.c

$(NXT_BUILDDIR)/nxt_md5.o: \
	$(NXT_LIB)/nxt_types.h \
	$(NXT_LIB)/nxt_clang.h \
	$(NXT_LIB)/nxt_md5.h \
	$(NXT_LIB)/nxt_md5.c \

	$(NXT_CC) -c -o $(NXT_BUILDDIR)/nxt_md5.o $(NXT_CFLAGS) \
		-I$(NXT_LIB) \
		$(NXT_LIB)/nxt_md5.c

$(NXT_BUILDDIR)/nxt_sha1.o: \
	$(NXT_LIB)/nxt_types.h \
	$(NXT_LIB)/nxt_clang.h \
	$(NXT_LIB)/nxt_sha1.h \
	$(NXT_LIB)/nxt_sha1.c \

	$(NXT_CC) -c -o $(NXT_BUILDDIR)/nxt_sha1.o $(NXT_CFLAGS) \
		-I$(NXT_LIB) \
		$(NXT_LIB)/nxt_sha1.c

$(NXT_BUILDDIR)/nxt_sha2.o: \
	$(NXT_LIB)/nxt_types.h \
	$(NXT_LIB)/nxt_clang.h \
	$(NXT_LIB)/nxt_sha2.h \
	$(NXT_LIB)/nxt_sha2.c \

	$(NXT_CC) -c -o $(NXT_BUILDDIR)/nxt_sha2.o $(NXT_CFLAGS) \
		-I$(NXT_LIB) \
		$(NXT_LIB)/nxt_sha2.c

$(NXT_BUILDDIR)/nxt_pcre.o: \
	$(NXT_LIB)/nxt_types.h \
	$(NXT_LIB)/nxt_clang.h \
	$(NXT_LIB)/nxt_trace.h \
	$(NXT_LIB)/nxt_regex.h \
	$(NXT_LIB)/nxt_pcre.h \
	$(NXT_LIB)/nxt_pcre.c \

	$(NXT_CC) -c -o $(NXT_BUILDDIR)/nxt_pcre.o $(NXT_CFLAGS) \
		-I$(NXT_LIB) $(NXT_PCRE_CFLAGS) \
		$(NXT_LIB)/nxt_pcre.c

$(NXT_BUILDDIR)/nxt_malloc.o: \
	$(NXT_LIB)/nxt_auto_config.h \
	$(NXT_LIB)/nxt_types.h \
	$(NXT_LIB)/nxt_clang.h \
	$(NXT_LIB)/nxt_malloc.h \
	$(NXT_LIB)/nxt_malloc.c \

	$(NXT_CC) -c -o $(NXT_BUILDDIR)/nxt_malloc.o $(NXT_CFLAGS) \
		-I$(NXT_LIB) \
		$(NXT_LIB)/nxt_malloc.c

$(NXT_BUILDDIR)/nxt_trace.o: \
	$(NXT_LIB)/nxt_auto_config.h \
	$(NXT_LIB)/nxt_types.h \
	$(NXT_LIB)/nxt_clang.h \
	$(NXT_LIB)/nxt_malloc.h \
	$(NXT_LIB)/nxt_trace.h \
	$(NXT_LIB)/nxt_trace.c \

	$(NXT_CC) -c -o $(NXT_BUILDDIR)/nxt_trace.o $(NXT_CFLAGS) \
		-I$(NXT_LIB) \
		$(NXT_LIB)/nxt_trace.c

$(NXT_BUILDDIR)/nxt_mem_cache_pool.o: \
	$(NXT_LIB)/nxt_types.h \
	$(NXT_LIB)/nxt_clang.h \
	$(NXT_LIB)/nxt_alignment.h \
	$(NXT_LIB)/nxt_queue.h \
	$(NXT_LIB)/nxt_rbtree.h \
	$(NXT_LIB)/nxt_mem_cache_pool.h \
	$(NXT_LIB)/nxt_mem_cache_pool.c \

	$(NXT_CC) -c -o $(NXT_BUILDDIR)/nxt_mem_cache_pool.o $(NXT_CFLAGS) \
		-I$(NXT_LIB) \
		$(NXT_LIB)/nxt_mem_cache_pool.c

include $(NXT_LIB)/test/Makefile
