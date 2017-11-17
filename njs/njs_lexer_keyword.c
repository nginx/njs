
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <nxt_auto_config.h>
#include <nxt_types.h>
#include <nxt_clang.h>
#include <nxt_string.h>
#include <nxt_stub.h>
#include <nxt_djb_hash.h>
#include <nxt_array.h>
#include <nxt_lvlhsh.h>
#include <nxt_random.h>
#include <nxt_mem_cache_pool.h>
#include <njscript.h>
#include <njs_vm.h>
#include <njs_number.h>
#include <njs_object.h>
#include <njs_variable.h>
#include <njs_parser.h>
#include <string.h>


static const njs_keyword_t  njs_keywords[] = {

    /* Values. */

    { nxt_string("undefined"),     NJS_TOKEN_UNDEFINED, 0 },
    { nxt_string("null"),          NJS_TOKEN_NULL, 0 },
    { nxt_string("false"),         NJS_TOKEN_BOOLEAN, 0 },
    { nxt_string("true"),          NJS_TOKEN_BOOLEAN, 1 },
    { nxt_string("NaN"),           NJS_TOKEN_NUMBER, NAN },
    { nxt_string("Infinity"),      NJS_TOKEN_NUMBER, INFINITY },

    /* Operators. */

    { nxt_string("in"),            NJS_TOKEN_IN, 0 },
    { nxt_string("typeof"),        NJS_TOKEN_TYPEOF, 0 },
    { nxt_string("instanceof"),    NJS_TOKEN_INSTANCEOF, 0 },
    { nxt_string("void"),          NJS_TOKEN_VOID, 0 },
    { nxt_string("new"),           NJS_TOKEN_NEW, 0 },
    { nxt_string("delete"),        NJS_TOKEN_DELETE, 0 },
    { nxt_string("yield"),         NJS_TOKEN_YIELD, 0 },

    /* Statements. */

    { nxt_string("var"),           NJS_TOKEN_VAR, 0 },
    { nxt_string("if"),            NJS_TOKEN_IF, 0 },
    { nxt_string("else"),          NJS_TOKEN_ELSE, 0 },
    { nxt_string("while"),         NJS_TOKEN_WHILE, 0 },
    { nxt_string("do"),            NJS_TOKEN_DO, 0 },
    { nxt_string("for"),           NJS_TOKEN_FOR, 0 },
    { nxt_string("break"),         NJS_TOKEN_BREAK, 0 },
    { nxt_string("continue"),      NJS_TOKEN_CONTINUE, 0 },
    { nxt_string("switch"),        NJS_TOKEN_SWITCH, 0 },
    { nxt_string("case"),          NJS_TOKEN_CASE, 0 },
    { nxt_string("default"),       NJS_TOKEN_DEFAULT, 0 },
    { nxt_string("function"),      NJS_TOKEN_FUNCTION, 0 },
    { nxt_string("return"),        NJS_TOKEN_RETURN, 0 },
    { nxt_string("with"),          NJS_TOKEN_WITH, 0 },
    { nxt_string("try"),           NJS_TOKEN_TRY, 0 },
    { nxt_string("catch"),         NJS_TOKEN_CATCH, 0 },
    { nxt_string("finally"),       NJS_TOKEN_FINALLY, 0 },
    { nxt_string("throw"),         NJS_TOKEN_THROW, 0 },

    /* Builtin objects. */

    { nxt_string("this"),          NJS_TOKEN_THIS, 0 },
    { nxt_string("Math"),          NJS_TOKEN_MATH, 0 },
    { nxt_string("JSON"),          NJS_TOKEN_JSON, 0 },

    /* Builtin functions. */

    { nxt_string("Object"),        NJS_TOKEN_OBJECT_CONSTRUCTOR, 0 },
    { nxt_string("Array"),         NJS_TOKEN_ARRAY_CONSTRUCTOR, 0 },
    { nxt_string("Boolean"),       NJS_TOKEN_BOOLEAN_CONSTRUCTOR, 0 },
    { nxt_string("Number"),        NJS_TOKEN_NUMBER_CONSTRUCTOR, 0 },
    { nxt_string("String"),        NJS_TOKEN_STRING_CONSTRUCTOR, 0 },
    { nxt_string("Function"),      NJS_TOKEN_FUNCTION_CONSTRUCTOR, 0 },
    { nxt_string("RegExp"),        NJS_TOKEN_REGEXP_CONSTRUCTOR, 0 },
    { nxt_string("Date"),          NJS_TOKEN_DATE_CONSTRUCTOR, 0 },
    { nxt_string("Error"),         NJS_TOKEN_ERROR_CONSTRUCTOR, 0 },
    { nxt_string("EvalError"),     NJS_TOKEN_EVAL_ERROR_CONSTRUCTOR, 0 },
    { nxt_string("InternalError"), NJS_TOKEN_INTERNAL_ERROR_CONSTRUCTOR, 0 },
    { nxt_string("RangeError"),    NJS_TOKEN_RANGE_ERROR_CONSTRUCTOR, 0 },
    { nxt_string("ReferenceError"), NJS_TOKEN_REF_ERROR_CONSTRUCTOR, 0 },
    { nxt_string("SyntaxError"),   NJS_TOKEN_SYNTAX_ERROR_CONSTRUCTOR, 0 },
    { nxt_string("TypeError"),     NJS_TOKEN_TYPE_ERROR_CONSTRUCTOR, 0 },
    { nxt_string("URIError"),      NJS_TOKEN_URI_ERROR_CONSTRUCTOR, 0 },
    { nxt_string("MemoryError"),   NJS_TOKEN_MEMORY_ERROR_CONSTRUCTOR, 0 },

    { nxt_string("eval"),          NJS_TOKEN_EVAL, 0 },
    { nxt_string("toString"),      NJS_TOKEN_TO_STRING, 0 },
    { nxt_string("isNaN"),         NJS_TOKEN_IS_NAN, 0 },
    { nxt_string("isFinite"),      NJS_TOKEN_IS_FINITE, 0 },
    { nxt_string("parseInt"),      NJS_TOKEN_PARSE_INT, 0 },
    { nxt_string("parseFloat"),    NJS_TOKEN_PARSE_FLOAT, 0 },
    { nxt_string("encodeURI"),     NJS_TOKEN_ENCODE_URI, 0 },
    { nxt_string("encodeURIComponent"),  NJS_TOKEN_ENCODE_URI_COMPONENT, 0 },
    { nxt_string("decodeURI"),     NJS_TOKEN_DECODE_URI, 0 },
    { nxt_string("decodeURIComponent"),  NJS_TOKEN_DECODE_URI_COMPONENT, 0 },
    { nxt_string("require"),      NJS_TOKEN_REQUIRE, 0 },

    /* Reserved words. */

    { nxt_string("abstract"),      NJS_TOKEN_RESERVED, 0 },
    { nxt_string("boolean"),       NJS_TOKEN_RESERVED, 0 },
    { nxt_string("byte"),          NJS_TOKEN_RESERVED, 0 },
    { nxt_string("char"),          NJS_TOKEN_RESERVED, 0 },
    { nxt_string("class"),         NJS_TOKEN_RESERVED, 0 },
    { nxt_string("const"),         NJS_TOKEN_RESERVED, 0 },
    { nxt_string("debugger"),      NJS_TOKEN_RESERVED, 0 },
    { nxt_string("double"),        NJS_TOKEN_RESERVED, 0 },
    { nxt_string("enum"),          NJS_TOKEN_RESERVED, 0 },
    { nxt_string("export"),        NJS_TOKEN_RESERVED, 0 },
    { nxt_string("extends"),       NJS_TOKEN_RESERVED, 0 },
    { nxt_string("final"),         NJS_TOKEN_RESERVED, 0 },
    { nxt_string("float"),         NJS_TOKEN_RESERVED, 0 },
    { nxt_string("goto"),          NJS_TOKEN_RESERVED, 0 },
    { nxt_string("implements"),    NJS_TOKEN_RESERVED, 0 },
    { nxt_string("import"),        NJS_TOKEN_RESERVED, 0 },
    { nxt_string("int"),           NJS_TOKEN_RESERVED, 0 },
    { nxt_string("interface"),     NJS_TOKEN_RESERVED, 0 },
    { nxt_string("long"),          NJS_TOKEN_RESERVED, 0 },
    { nxt_string("native"),        NJS_TOKEN_RESERVED, 0 },
    { nxt_string("package"),       NJS_TOKEN_RESERVED, 0 },
    { nxt_string("private"),       NJS_TOKEN_RESERVED, 0 },
    { nxt_string("protected"),     NJS_TOKEN_RESERVED, 0 },
    { nxt_string("public"),        NJS_TOKEN_RESERVED, 0 },
    { nxt_string("short"),         NJS_TOKEN_RESERVED, 0 },
    { nxt_string("static"),        NJS_TOKEN_RESERVED, 0 },
    { nxt_string("super"),         NJS_TOKEN_RESERVED, 0 },
    { nxt_string("synchronized"),  NJS_TOKEN_RESERVED, 0 },
    { nxt_string("throws"),        NJS_TOKEN_RESERVED, 0 },
    { nxt_string("transient"),     NJS_TOKEN_RESERVED, 0 },
    { nxt_string("volatile"),      NJS_TOKEN_RESERVED, 0 },
};


static nxt_int_t
njs_keyword_hash_test(nxt_lvlhsh_query_t *lhq, void *data)
{
    njs_keyword_t  *keyword;

    keyword = data;

    if (nxt_strstr_eq(&lhq->key, &keyword->name)) {
        return NXT_OK;
    }

    return NXT_DECLINED;
}


const nxt_lvlhsh_proto_t  njs_keyword_hash_proto
    nxt_aligned(64) =
{
    NXT_LVLHSH_DEFAULT,
    0,
    njs_keyword_hash_test,
    njs_lvlhsh_alloc,
    njs_lvlhsh_free,
};


nxt_int_t
njs_lexer_keywords_init(nxt_mem_cache_pool_t *mcp, nxt_lvlhsh_t *hash)
{
    nxt_uint_t           n;
    nxt_lvlhsh_query_t   lhq;
    const njs_keyword_t  *keyword;

    keyword = njs_keywords;
    n = nxt_nitems(njs_keywords);

    lhq.replace = 0;
    lhq.proto = &njs_keyword_hash_proto;
    lhq.pool = mcp;

    do {
        lhq.key_hash = nxt_djb_hash(keyword->name.start, keyword->name.length);
        lhq.key = keyword->name;
        lhq.value = (void *) keyword;

        if (nxt_slow_path(nxt_lvlhsh_insert(hash, &lhq) != NXT_OK)) {
            return NXT_ERROR;
        }

        keyword++;
        n--;

    } while (n != 0);

    return NXT_OK;
}


njs_token_t
njs_lexer_keyword(njs_lexer_t *lexer)
{
    njs_keyword_t       *keyword;
    nxt_lvlhsh_query_t  lhq;

    lhq.key_hash = lexer->key_hash;
    lhq.key = lexer->text;
    lhq.proto = &njs_keyword_hash_proto;

    if (nxt_lvlhsh_find(&lexer->keywords_hash, &lhq) == NXT_OK) {
        keyword = lhq.value;
        lexer->number = keyword->number;

        return keyword->token;
    }

    return NJS_TOKEN_NAME;
}
