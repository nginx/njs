
NJS_VER =	0.1.15

NXT_LIB =	nxt

-include	$(NXT_LIB)/Makefile.conf

NXT_BUILDDIR =	build

$(NXT_BUILDDIR)/libnjs.a: \
	$(NXT_LIB)/nxt_auto_config.h \
	$(NXT_BUILDDIR)/njscript.o \
	$(NXT_BUILDDIR)/njs_vm.o \
	$(NXT_BUILDDIR)/njs_boolean.o \
	$(NXT_BUILDDIR)/njs_number.o \
	$(NXT_BUILDDIR)/njs_string.o \
	$(NXT_BUILDDIR)/njs_object.o \
	$(NXT_BUILDDIR)/njs_array.o \
	$(NXT_BUILDDIR)/njs_json.o \
	$(NXT_BUILDDIR)/njs_function.o \
	$(NXT_BUILDDIR)/njs_regexp.o \
	$(NXT_BUILDDIR)/njs_date.o \
	$(NXT_BUILDDIR)/njs_error.o \
	$(NXT_BUILDDIR)/njs_math.o \
	$(NXT_BUILDDIR)/njs_module.o \
	$(NXT_BUILDDIR)/njs_fs.o \
	$(NXT_BUILDDIR)/njs_extern.o \
	$(NXT_BUILDDIR)/njs_variable.o \
	$(NXT_BUILDDIR)/njs_builtin.o \
	$(NXT_BUILDDIR)/njs_lexer.o \
	$(NXT_BUILDDIR)/njs_lexer_keyword.o \
	$(NXT_BUILDDIR)/njs_parser.o \
	$(NXT_BUILDDIR)/njs_parser_expression.o \
	$(NXT_BUILDDIR)/njs_generator.o \
	$(NXT_BUILDDIR)/njs_disassembler.o \
	$(NXT_BUILDDIR)/nxt_djb_hash.o \
	$(NXT_BUILDDIR)/nxt_utf8.o \
	$(NXT_BUILDDIR)/nxt_array.o \
	$(NXT_BUILDDIR)/nxt_rbtree.o \
	$(NXT_BUILDDIR)/nxt_lvlhsh.o \
	$(NXT_BUILDDIR)/nxt_trace.o \
	$(NXT_BUILDDIR)/nxt_random.o \
	$(NXT_BUILDDIR)/nxt_pcre.o \
	$(NXT_BUILDDIR)/nxt_malloc.o \
	$(NXT_BUILDDIR)/nxt_mem_cache_pool.o \

	ar -r -c $(NXT_BUILDDIR)/libnjs.a \
		$(NXT_BUILDDIR)/njscript.o \
		$(NXT_BUILDDIR)/njs_vm.o \
		$(NXT_BUILDDIR)/njs_boolean.o \
		$(NXT_BUILDDIR)/njs_number.o \
		$(NXT_BUILDDIR)/njs_string.o \
		$(NXT_BUILDDIR)/njs_object.o \
		$(NXT_BUILDDIR)/njs_array.o \
		$(NXT_BUILDDIR)/njs_json.o \
		$(NXT_BUILDDIR)/njs_function.o \
		$(NXT_BUILDDIR)/njs_regexp.o \
		$(NXT_BUILDDIR)/njs_date.o \
		$(NXT_BUILDDIR)/njs_error.o \
		$(NXT_BUILDDIR)/njs_math.o \
		$(NXT_BUILDDIR)/njs_module.o \
		$(NXT_BUILDDIR)/njs_fs.o \
		$(NXT_BUILDDIR)/njs_extern.o \
		$(NXT_BUILDDIR)/njs_variable.o \
		$(NXT_BUILDDIR)/njs_builtin.o \
		$(NXT_BUILDDIR)/njs_lexer.o \
		$(NXT_BUILDDIR)/njs_lexer_keyword.o \
		$(NXT_BUILDDIR)/njs_parser.o \
		$(NXT_BUILDDIR)/njs_parser_expression.o \
		$(NXT_BUILDDIR)/njs_generator.o \
		$(NXT_BUILDDIR)/njs_disassembler.o \
		$(NXT_BUILDDIR)/nxt_djb_hash.o \
		$(NXT_BUILDDIR)/nxt_utf8.o \
		$(NXT_BUILDDIR)/nxt_array.o \
		$(NXT_BUILDDIR)/nxt_rbtree.o \
		$(NXT_BUILDDIR)/nxt_lvlhsh.o \
		$(NXT_BUILDDIR)/nxt_trace.o \
		$(NXT_BUILDDIR)/nxt_random.o \
		$(NXT_BUILDDIR)/nxt_pcre.o \
		$(NXT_BUILDDIR)/nxt_malloc.o \
		$(NXT_BUILDDIR)/nxt_mem_cache_pool.o \

all:	test lib_test

njs:	$(NXT_BUILDDIR)/njs

libnjs:	$(NXT_BUILDDIR)/libnjs.a

njs_interactive_test:	njs_expect_test $(NXT_BUILDDIR)/njs_interactive_test
	$(NXT_BUILDDIR)/njs_interactive_test

test:	njs_interactive_test \
	$(NXT_BUILDDIR)/njs_unit_test \
	$(NXT_BUILDDIR)/njs_benchmark \

	$(NXT_BUILDDIR)/njs_unit_test d

clean:
	rm -rf $(NXT_BUILDDIR)
	rm -f $(NXT_LIB)/Makefile.conf $(NXT_LIB)/nxt_auto_config.h

dist:
	make clean
	mkdir njs-$(NJS_VER)
	cp -rp configure Makefile LICENSE README CHANGES $(NXT_LIB) njs nginx \
		njs-$(NJS_VER)
	tar czf njs-$(NJS_VER).tar.gz njs-$(NJS_VER)
	rm -rf njs-$(NJS_VER)

$(NXT_LIB)/nxt_auto_config.h:
	@echo
	@echo "	Please run ./configure before make"
	@echo
	@exit 1

$(NXT_BUILDDIR)/njscript.o: \
	$(NXT_BUILDDIR)/libnxt.a \
	njs/njs_vm.h \
	njs/njs_string.h \
	njs/njs_object.h \
	njs/njs_function.h \
	njs/njs_parser.h \
	njs/njscript.h \
	njs/njscript.c \

	$(NXT_CC) -c -o $(NXT_BUILDDIR)/njscript.o $(NXT_CFLAGS) \
		-I$(NXT_LIB) -Injs \
		njs/njscript.c

$(NXT_BUILDDIR)/njs_vm.o: \
	$(NXT_BUILDDIR)/libnxt.a \
	njs/njscript.h \
	njs/njs_vm.h \
	njs/njs_number.h \
	njs/njs_string.h \
	njs/njs_object.h \
	njs/njs_object_hash.h \
	njs/njs_array.h \
	njs/njs_function.h \
	njs/njs_regexp.h \
	njs/njs_extern.h \
	njs/njs_variable.h \
	njs/njs_parser.h \
	njs/njs_vm.c \

	$(NXT_CC) -c -o $(NXT_BUILDDIR)/njs_vm.o $(NXT_CFLAGS) \
		-I$(NXT_LIB) -Injs \
		njs/njs_vm.c

$(NXT_BUILDDIR)/njs_boolean.o: \
	$(NXT_BUILDDIR)/libnxt.a \
	njs/njscript.h \
	njs/njs_vm.h \
	njs/njs_boolean.h \
	njs/njs_object.h \
	njs/njs_function.h \
	njs/njs_boolean.c \

	$(NXT_CC) -c -o $(NXT_BUILDDIR)/njs_boolean.o $(NXT_CFLAGS) \
		-I$(NXT_LIB) -Injs \
		njs/njs_boolean.c

$(NXT_BUILDDIR)/njs_number.o: \
	$(NXT_BUILDDIR)/libnxt.a \
	njs/njscript.h \
	njs/njs_vm.h \
	njs/njs_number.h \
	njs/njs_string.h \
	njs/njs_object.h \
	njs/njs_array.h \
	njs/njs_function.h \
	njs/njs_number.c \

	$(NXT_CC) -c -o $(NXT_BUILDDIR)/njs_number.o $(NXT_CFLAGS) \
		-I$(NXT_LIB) -Injs \
		njs/njs_number.c

$(NXT_BUILDDIR)/njs_string.o: \
	$(NXT_BUILDDIR)/libnxt.a \
	njs/njscript.h \
	njs/njs_vm.h \
	njs/njs_number.h \
	njs/njs_string.h \
	njs/njs_object.h \
	njs/njs_object_hash.h \
	njs/njs_array.h \
	njs/njs_function.h \
	njs/njs_regexp.h \
	njs/njs_parser.h \
	njs/njs_string.c \

	$(NXT_CC) -c -o $(NXT_BUILDDIR)/njs_string.o $(NXT_CFLAGS) \
		-I$(NXT_LIB) -Injs $(NXT_PCRE_CFLAGS) \
		njs/njs_string.c

$(NXT_BUILDDIR)/njs_object.o: \
	$(NXT_BUILDDIR)/libnxt.a \
	njs/njscript.h \
	njs/njs_vm.h \
	njs/njs_object.h \
	njs/njs_object_hash.h \
	njs/njs_function.h \
	njs/njs_object.c \

	$(NXT_CC) -c -o $(NXT_BUILDDIR)/njs_object.o $(NXT_CFLAGS) \
		-I$(NXT_LIB) -Injs \
		njs/njs_object.c

$(NXT_BUILDDIR)/njs_array.o: \
	$(NXT_BUILDDIR)/libnxt.a \
	njs/njscript.h \
	njs/njs_vm.h \
	njs/njs_number.h \
	njs/njs_string.h \
	njs/njs_object.h \
	njs/njs_object_hash.h \
	njs/njs_array.h \
	njs/njs_function.h \
	njs/njs_array.c \

	$(NXT_CC) -c -o $(NXT_BUILDDIR)/njs_array.o $(NXT_CFLAGS) \
		-I$(NXT_LIB) -Injs \
		njs/njs_array.c

$(NXT_BUILDDIR)/njs_json.o: \
	$(NXT_BUILDDIR)/libnxt.a \
	njs/njscript.h \
	njs/njs_vm.h \
	njs/njs_object.h \
	njs/njs_json.c \

	$(NXT_CC) -c -o $(NXT_BUILDDIR)/njs_json.o $(NXT_CFLAGS) \
		-I$(NXT_LIB) -Injs \
		njs/njs_json.c


$(NXT_BUILDDIR)/njs_function.o: \
	$(NXT_BUILDDIR)/libnxt.a \
	njs/njscript.h \
	njs/njs_vm.h \
	njs/njs_object.h \
	njs/njs_array.h \
	njs/njs_function.h \
	njs/njs_function.c \

	$(NXT_CC) -c -o $(NXT_BUILDDIR)/njs_function.o $(NXT_CFLAGS) \
		-I$(NXT_LIB) -Injs \
		njs/njs_function.c

$(NXT_BUILDDIR)/njs_regexp.o: \
	$(NXT_BUILDDIR)/libnxt.a \
	njs/njscript.h \
	njs/njs_vm.h \
	njs/njs_string.h \
	njs/njs_object.h \
	njs/njs_object_hash.h \
	njs/njs_array.h \
	njs/njs_function.h \
	njs/njs_regexp.h \
	njs/njs_regexp.c \

	$(NXT_CC) -c -o $(NXT_BUILDDIR)/njs_regexp.o $(NXT_CFLAGS) \
		-I$(NXT_LIB) -Injs $(NXT_PCRE_CFLAGS) \
		njs/njs_regexp.c

$(NXT_BUILDDIR)/njs_date.o: \
	$(NXT_BUILDDIR)/libnxt.a \
	njs/njscript.h \
	njs/njs_vm.h \
	njs/njs_string.h \
	njs/njs_object.h \
	njs/njs_function.h \
	njs/njs_date.h \
	njs/njs_date.c \

	$(NXT_CC) -c -o $(NXT_BUILDDIR)/njs_date.o $(NXT_CFLAGS) \
		-I$(NXT_LIB) -Injs $(NXT_PCRE_CFLAGS) \
		njs/njs_date.c

$(NXT_BUILDDIR)/njs_error.o: \
	$(NXT_BUILDDIR)/libnxt.a \
	njs/njscript.h \
	njs/njs_vm.h \
	njs/njs_string.h \
	njs/njs_object.h \
	njs/njs_function.h \
	njs/njs_error.h \
	njs/njs_error.c \

	$(NXT_CC) -c -o $(NXT_BUILDDIR)/njs_error.o $(NXT_CFLAGS) \
		-I$(NXT_LIB) -Injs $(NXT_PCRE_CFLAGS) \
		njs/njs_error.c

$(NXT_BUILDDIR)/njs_math.o: \
	$(NXT_BUILDDIR)/libnxt.a \
	njs/njscript.h \
	njs/njs_vm.h \
	njs/njs_object.h \
	njs/njs_math.h \
	njs/njs_math.c \

	$(NXT_CC) -c -o $(NXT_BUILDDIR)/njs_math.o $(NXT_CFLAGS) \
		-I$(NXT_LIB) -Injs \
		njs/njs_math.c

$(NXT_BUILDDIR)/njs_module.o: \
	$(NXT_BUILDDIR)/libnxt.a \
	njs/njscript.h \
	njs/njs_vm.h \
	njs/njs_module.h \
	njs/njs_module.c \

	$(NXT_CC) -c -o $(NXT_BUILDDIR)/njs_module.o $(NXT_CFLAGS) \
		-I$(NXT_LIB) -Injs \
		njs/njs_module.c

$(NXT_BUILDDIR)/njs_fs.o: \
	$(NXT_BUILDDIR)/libnxt.a \
	njs/njscript.h \
	njs/njs_vm.h \
	njs/njs_fs.h \
	njs/njs_fs.c \

	$(NXT_CC) -c -o $(NXT_BUILDDIR)/njs_fs.o $(NXT_CFLAGS) \
		-I$(NXT_LIB) -Injs \
		njs/njs_fs.c

$(NXT_BUILDDIR)/njs_extern.o: \
	$(NXT_BUILDDIR)/libnxt.a \
	njs/njscript.h \
	njs/njs_vm.h \
	njs/njs_parser.h \
	njs/njs_extern.h \
	njs/njs_extern.c \

	$(NXT_CC) -c -o $(NXT_BUILDDIR)/njs_extern.o $(NXT_CFLAGS) \
		-I$(NXT_LIB) -Injs \
		njs/njs_extern.c

$(NXT_BUILDDIR)/njs_variable.o: \
	$(NXT_BUILDDIR)/libnxt.a \
	njs/njscript.h \
	njs/njs_vm.h \
	njs/njs_parser.h \
	njs/njs_variable.h \
	njs/njs_variable.c \

	$(NXT_CC) -c -o $(NXT_BUILDDIR)/njs_variable.o $(NXT_CFLAGS) \
		-I$(NXT_LIB) -Injs \
		njs/njs_variable.c

$(NXT_BUILDDIR)/njs_builtin.o: \
	$(NXT_BUILDDIR)/libnxt.a \
	njs/njscript.h \
	njs/njs_vm.h \
	njs/njs_boolean.h \
	njs/njs_number.h \
	njs/njs_string.h \
	njs/njs_object.h \
	njs/njs_array.h \
	njs/njs_module.h \
	njs/njs_function.h \
	njs/njs_regexp.h \
	njs/njs_parser.h \
	njs/njs_builtin.c \

	$(NXT_CC) -c -o $(NXT_BUILDDIR)/njs_builtin.o $(NXT_CFLAGS) \
		-I$(NXT_LIB) -Injs \
		njs/njs_builtin.c

$(NXT_BUILDDIR)/njs_lexer.o: \
	$(NXT_BUILDDIR)/libnxt.a \
	njs/njscript.h \
	njs/njs_vm.h \
	njs/njs_parser.h \
	njs/njs_lexer.c \

	$(NXT_CC) -c -o $(NXT_BUILDDIR)/njs_lexer.o $(NXT_CFLAGS) \
		-I$(NXT_LIB) -Injs \
		njs/njs_lexer.c

$(NXT_BUILDDIR)/njs_lexer_keyword.o: \
	$(NXT_BUILDDIR)/libnxt.a \
	njs/njscript.h \
	njs/njs_vm.h \
	njs/njs_number.h \
	njs/njs_object.h \
	njs/njs_parser.h \
	njs/njs_lexer_keyword.c \

	$(NXT_CC) -c -o $(NXT_BUILDDIR)/njs_lexer_keyword.o $(NXT_CFLAGS) \
		-I$(NXT_LIB) -Injs \
		njs/njs_lexer_keyword.c

$(NXT_BUILDDIR)/njs_parser.o: \
	$(NXT_BUILDDIR)/libnxt.a \
	njs/njscript.h \
	njs/njs_vm.h \
	njs/njs_string.h \
	njs/njs_object.h \
	njs/njs_function.h \
	njs/njs_variable.h \
	njs/njs_parser.h \
	njs/njs_parser.c \

	$(NXT_CC) -c -o $(NXT_BUILDDIR)/njs_parser.o $(NXT_CFLAGS) \
		-I$(NXT_LIB) -Injs \
		njs/njs_parser.c \

$(NXT_BUILDDIR)/njs_parser_expression.o: \
	$(NXT_BUILDDIR)/libnxt.a \
	njs/njscript.h \
	njs/njs_vm.h \
	njs/njs_number.h \
	njs/njs_object.h \
	njs/njs_function.h \
	njs/njs_variable.h \
	njs/njs_parser.h \
	njs/njs_parser_expression.c \

	$(NXT_CC) -c -o $(NXT_BUILDDIR)/njs_parser_expression.o \
		$(NXT_CFLAGS) -I$(NXT_LIB) -Injs \
		njs/njs_parser_expression.c

$(NXT_BUILDDIR)/njs_generator.o: \
	$(NXT_BUILDDIR)/libnxt.a \
	njs/njscript.h \
	njs/njs_vm.h \
	njs/njs_number.h \
	njs/njs_string.h \
	njs/njs_object.h \
	njs/njs_function.h \
	njs/njs_variable.h \
	njs/njs_parser.h \
	njs/njs_generator.c \

	$(NXT_CC) -c -o $(NXT_BUILDDIR)/njs_generator.o $(NXT_CFLAGS) \
		-I$(NXT_LIB) -Injs \
		njs/njs_generator.c

$(NXT_BUILDDIR)/njs_disassembler.o: \
	$(NXT_BUILDDIR)/libnxt.a \
	njs/njscript.h \
	njs/njs_vm.h \
	njs/njs_object.h \
	njs/njs_parser.h \
	njs/njs_disassembler.c \

	$(NXT_CC) -c -o $(NXT_BUILDDIR)/njs_disassembler.o $(NXT_CFLAGS) \
		-I$(NXT_LIB) -Injs \
		njs/njs_disassembler.c

$(NXT_BUILDDIR)/njs: \
	$(NXT_BUILDDIR)/libnxt.a \
	$(NXT_BUILDDIR)/libnjs.a \
	njs/njs.c \

	$(NXT_CC) -o $(NXT_BUILDDIR)/njs $(NXT_CFLAGS) \
		-I$(NXT_LIB) $(NXT_EDITLINE_CFLAGS) -Injs \
		njs/njs.c \
		$(NXT_BUILDDIR)/libnjs.a \
		-lm $(NXT_PCRE_LIB) $(NXT_EDITLINE_LIB)

$(NXT_BUILDDIR)/njs_unit_test: \
	$(NXT_BUILDDIR)/libnxt.a \
	$(NXT_BUILDDIR)/libnjs.a \
	njs/test/njs_unit_test.c \

	$(NXT_CC) -o $(NXT_BUILDDIR)/njs_unit_test $(NXT_CFLAGS) \
		-I$(NXT_LIB) -Injs \
		njs/test/njs_unit_test.c \
		$(NXT_BUILDDIR)/libnjs.a \
		-lm $(NXT_PCRE_LIB)

$(NXT_BUILDDIR)/njs_interactive_test: \
	$(NXT_BUILDDIR)/libnxt.a \
	$(NXT_BUILDDIR)/libnjs.a \
	njs/test/njs_interactive_test.c \

	$(NXT_CC) -o $(NXT_BUILDDIR)/njs_interactive_test $(NXT_CFLAGS) \
		-I$(NXT_LIB) -Injs \
		njs/test/njs_interactive_test.c \
		$(NXT_BUILDDIR)/libnjs.a \
		-lm $(NXT_PCRE_LIB)

$(NXT_BUILDDIR)/njs_benchmark: \
	$(NXT_BUILDDIR)/libnxt.a \
	$(NXT_BUILDDIR)/libnjs.a \
	njs/test/njs_benchmark.c \

	$(NXT_CC) -o $(NXT_BUILDDIR)/njs_benchmark $(NXT_CFLAGS) \
		-I$(NXT_LIB) -Injs \
		njs/test/njs_benchmark.c \
		$(NXT_BUILDDIR)/libnjs.a \
		-lm $(NXT_PCRE_LIB)

include $(NXT_LIB)/Makefile
