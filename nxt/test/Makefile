
lib_test: \
	$(NXT_LIB)/nxt_auto_config.h \
	$(NXT_BUILDDIR)/random_unit_test \
	$(NXT_BUILDDIR)/rbtree_unit_test \
	$(NXT_BUILDDIR)/lvlhsh_unit_test \
	$(NXT_BUILDDIR)/utf8_unit_test \

	$(NXT_BUILDDIR)/random_unit_test
	$(NXT_BUILDDIR)/rbtree_unit_test
	$(NXT_BUILDDIR)/lvlhsh_unit_test
	$(NXT_BUILDDIR)/utf8_unit_test

$(NXT_BUILDDIR)/utf8_unit_test: \
	$(NXT_BUILDDIR)/nxt_utf8.o \
	$(NXT_LIB)/test/utf8_unit_test.c \

	$(NXT_CC) -o $(NXT_BUILDDIR)/utf8_unit_test $(NXT_CFLAGS) \
		-I$(NXT_LIB) \
		$(NXT_LIB)/test/utf8_unit_test.c \
		$(NXT_BUILDDIR)/nxt_utf8.o

$(NXT_BUILDDIR)/rbtree_unit_test: \
	$(NXT_BUILDDIR)/nxt_rbtree.o \
	$(NXT_BUILDDIR)/nxt_murmur_hash.o \
	$(NXT_LIB)/test/rbtree_unit_test.c \

	$(NXT_CC) -o $(NXT_BUILDDIR)/rbtree_unit_test $(NXT_CFLAGS) \
		-I$(NXT_LIB) \
		$(NXT_LIB)/test/rbtree_unit_test.c \
		$(NXT_BUILDDIR)/nxt_rbtree.o \
		$(NXT_BUILDDIR)/nxt_murmur_hash.o

$(NXT_BUILDDIR)/lvlhsh_unit_test: \
	$(NXT_BUILDDIR)/nxt_lvlhsh.o \
	$(NXT_BUILDDIR)/nxt_murmur_hash.o \
	$(NXT_BUILDDIR)/nxt_mem_cache_pool.o \
	$(NXT_BUILDDIR)/nxt_malloc.o \
	$(NXT_LIB)/test/lvlhsh_unit_test.c \

	$(NXT_CC) -o $(NXT_BUILDDIR)/lvlhsh_unit_test $(NXT_CFLAGS) \
		-I$(NXT_LIB) \
		$(NXT_LIB)/test/lvlhsh_unit_test.c \
		$(NXT_BUILDDIR)/nxt_lvlhsh.o \
		$(NXT_BUILDDIR)/nxt_rbtree.o \
		$(NXT_BUILDDIR)/nxt_murmur_hash.o \
		$(NXT_BUILDDIR)/nxt_mem_cache_pool.o \
		$(NXT_BUILDDIR)/nxt_malloc.o

$(NXT_BUILDDIR)/random_unit_test: \
	$(NXT_BUILDDIR)/nxt_random.o \
	$(NXT_LIB)/test/random_unit_test.c \

	$(NXT_CC) -o $(NXT_BUILDDIR)/random_unit_test $(NXT_CFLAGS) \
		-I$(NXT_LIB) \
		$(NXT_LIB)/test/random_unit_test.c \
		$(NXT_BUILDDIR)/nxt_random.o
