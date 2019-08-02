
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


static const njs_keyword_t  njs_keywords[] = {

    /* Values. */

    { njs_str("undefined"),     NJS_TOKEN_UNDEFINED, 0 },
    { njs_str("null"),          NJS_TOKEN_NULL, 0 },
    { njs_str("false"),         NJS_TOKEN_BOOLEAN, 0 },
    { njs_str("true"),          NJS_TOKEN_BOOLEAN, 1 },
    { njs_str("NaN"),           NJS_TOKEN_NUMBER, NAN },
    { njs_str("Infinity"),      NJS_TOKEN_NUMBER, INFINITY },

    /* Operators. */

    { njs_str("in"),            NJS_TOKEN_IN, 0 },
    { njs_str("typeof"),        NJS_TOKEN_TYPEOF, 0 },
    { njs_str("instanceof"),    NJS_TOKEN_INSTANCEOF, 0 },
    { njs_str("void"),          NJS_TOKEN_VOID, 0 },
    { njs_str("new"),           NJS_TOKEN_NEW, 0 },
    { njs_str("delete"),        NJS_TOKEN_DELETE, 0 },
    { njs_str("yield"),         NJS_TOKEN_YIELD, 0 },

    /* Statements. */

    { njs_str("var"),           NJS_TOKEN_VAR, 0 },
    { njs_str("if"),            NJS_TOKEN_IF, 0 },
    { njs_str("else"),          NJS_TOKEN_ELSE, 0 },
    { njs_str("while"),         NJS_TOKEN_WHILE, 0 },
    { njs_str("do"),            NJS_TOKEN_DO, 0 },
    { njs_str("for"),           NJS_TOKEN_FOR, 0 },
    { njs_str("break"),         NJS_TOKEN_BREAK, 0 },
    { njs_str("continue"),      NJS_TOKEN_CONTINUE, 0 },
    { njs_str("switch"),        NJS_TOKEN_SWITCH, 0 },
    { njs_str("case"),          NJS_TOKEN_CASE, 0 },
    { njs_str("default"),       NJS_TOKEN_DEFAULT, 0 },
    { njs_str("function"),      NJS_TOKEN_FUNCTION, 0 },
    { njs_str("return"),        NJS_TOKEN_RETURN, 0 },
    { njs_str("with"),          NJS_TOKEN_WITH, 0 },
    { njs_str("try"),           NJS_TOKEN_TRY, 0 },
    { njs_str("catch"),         NJS_TOKEN_CATCH, 0 },
    { njs_str("finally"),       NJS_TOKEN_FINALLY, 0 },
    { njs_str("throw"),         NJS_TOKEN_THROW, 0 },

    /* Builtin objects. */

    { njs_str("this"),          NJS_TOKEN_THIS, 0 },
    { njs_str("arguments"),     NJS_TOKEN_ARGUMENTS, 0 },
    { njs_str("njs"),           NJS_TOKEN_NJS, 0 },
    { njs_str("process"),       NJS_TOKEN_PROCESS, 0 },
    { njs_str("Math"),          NJS_TOKEN_MATH, 0 },
    { njs_str("JSON"),          NJS_TOKEN_JSON, 0 },

    /* Builtin functions. */

    { njs_str("Object"),        NJS_TOKEN_OBJECT_CONSTRUCTOR, 0 },
    { njs_str("Array"),         NJS_TOKEN_ARRAY_CONSTRUCTOR, 0 },
    { njs_str("Boolean"),       NJS_TOKEN_BOOLEAN_CONSTRUCTOR, 0 },
    { njs_str("Number"),        NJS_TOKEN_NUMBER_CONSTRUCTOR, 0 },
    { njs_str("String"),        NJS_TOKEN_STRING_CONSTRUCTOR, 0 },
    { njs_str("Function"),      NJS_TOKEN_FUNCTION_CONSTRUCTOR, 0 },
    { njs_str("RegExp"),        NJS_TOKEN_REGEXP_CONSTRUCTOR, 0 },
    { njs_str("Date"),          NJS_TOKEN_DATE_CONSTRUCTOR, 0 },
    { njs_str("Error"),         NJS_TOKEN_ERROR_CONSTRUCTOR, 0 },
    { njs_str("EvalError"),     NJS_TOKEN_EVAL_ERROR_CONSTRUCTOR, 0 },
    { njs_str("InternalError"), NJS_TOKEN_INTERNAL_ERROR_CONSTRUCTOR, 0 },
    { njs_str("RangeError"),    NJS_TOKEN_RANGE_ERROR_CONSTRUCTOR, 0 },
    { njs_str("ReferenceError"), NJS_TOKEN_REF_ERROR_CONSTRUCTOR, 0 },
    { njs_str("SyntaxError"),   NJS_TOKEN_SYNTAX_ERROR_CONSTRUCTOR, 0 },
    { njs_str("TypeError"),     NJS_TOKEN_TYPE_ERROR_CONSTRUCTOR, 0 },
    { njs_str("URIError"),      NJS_TOKEN_URI_ERROR_CONSTRUCTOR, 0 },
    { njs_str("MemoryError"),   NJS_TOKEN_MEMORY_ERROR_CONSTRUCTOR, 0 },

    { njs_str("eval"),          NJS_TOKEN_EVAL, 0 },
    { njs_str("toString"),      NJS_TOKEN_TO_STRING, 0 },
    { njs_str("isNaN"),         NJS_TOKEN_IS_NAN, 0 },
    { njs_str("isFinite"),      NJS_TOKEN_IS_FINITE, 0 },
    { njs_str("parseInt"),      NJS_TOKEN_PARSE_INT, 0 },
    { njs_str("parseFloat"),    NJS_TOKEN_PARSE_FLOAT, 0 },
    { njs_str("encodeURI"),     NJS_TOKEN_ENCODE_URI, 0 },
    { njs_str("encodeURIComponent"),  NJS_TOKEN_ENCODE_URI_COMPONENT, 0 },
    { njs_str("decodeURI"),     NJS_TOKEN_DECODE_URI, 0 },
    { njs_str("decodeURIComponent"),  NJS_TOKEN_DECODE_URI_COMPONENT, 0 },
    { njs_str("require"),       NJS_TOKEN_REQUIRE, 0 },
    { njs_str("setTimeout"),    NJS_TOKEN_SET_TIMEOUT, 0 },
    { njs_str("setImmediate"),  NJS_TOKEN_SET_IMMEDIATE, 0 },
    { njs_str("clearTimeout"),  NJS_TOKEN_CLEAR_TIMEOUT, 0 },

    /* Module. */
    { njs_str("import"),        NJS_TOKEN_IMPORT, 0 },
    { njs_str("from"),          NJS_TOKEN_FROM, 0 },
    { njs_str("export"),        NJS_TOKEN_EXPORT, 0 },

    /* Reserved words. */

    { njs_str("await"),         NJS_TOKEN_RESERVED, 0 },
    { njs_str("class"),         NJS_TOKEN_RESERVED, 0 },
    { njs_str("const"),         NJS_TOKEN_RESERVED, 0 },
    { njs_str("debugger"),      NJS_TOKEN_RESERVED, 0 },
    { njs_str("enum"),          NJS_TOKEN_RESERVED, 0 },
    { njs_str("extends"),       NJS_TOKEN_RESERVED, 0 },
    { njs_str("implements"),    NJS_TOKEN_RESERVED, 0 },
    { njs_str("interface"),     NJS_TOKEN_RESERVED, 0 },
    { njs_str("let"),           NJS_TOKEN_RESERVED, 0 },
    { njs_str("package"),       NJS_TOKEN_RESERVED, 0 },
    { njs_str("private"),       NJS_TOKEN_RESERVED, 0 },
    { njs_str("protected"),     NJS_TOKEN_RESERVED, 0 },
    { njs_str("public"),        NJS_TOKEN_RESERVED, 0 },
    { njs_str("static"),        NJS_TOKEN_RESERVED, 0 },
    { njs_str("super"),         NJS_TOKEN_RESERVED, 0 },
};


static njs_int_t
njs_keyword_hash_test(njs_lvlhsh_query_t *lhq, void *data)
{
    njs_keyword_t  *keyword;

    keyword = data;

    if (njs_strstr_eq(&lhq->key, &keyword->name)) {
        return NJS_OK;
    }

    return NJS_DECLINED;
}


const njs_lvlhsh_proto_t  njs_keyword_hash_proto
    njs_aligned(64) =
{
    NJS_LVLHSH_DEFAULT,
    njs_keyword_hash_test,
    njs_lvlhsh_alloc,
    njs_lvlhsh_free,
};


njs_int_t
njs_lexer_keywords_init(njs_mp_t *mp, njs_lvlhsh_t *hash)
{
    njs_uint_t           n;
    njs_lvlhsh_query_t   lhq;
    const njs_keyword_t  *keyword;

    keyword = njs_keywords;
    n = njs_nitems(njs_keywords);

    lhq.replace = 0;
    lhq.proto = &njs_keyword_hash_proto;
    lhq.pool = mp;

    do {
        lhq.key_hash = njs_djb_hash(keyword->name.start, keyword->name.length);
        lhq.key = keyword->name;
        lhq.value = (void *) keyword;

        if (njs_slow_path(njs_lvlhsh_insert(hash, &lhq) != NJS_OK)) {
            return NJS_ERROR;
        }

        keyword++;
        n--;

    } while (n != 0);

    return NJS_OK;
}


void
njs_lexer_keyword(njs_lexer_t *lexer, njs_lexer_token_t *lt)
{
    njs_keyword_t       *keyword;
    njs_lvlhsh_query_t  lhq;

    lhq.key_hash = lt->key_hash;
    lhq.key = lt->text;
    lhq.proto = &njs_keyword_hash_proto;

    if (njs_lvlhsh_find(&lexer->keywords_hash, &lhq) == NJS_OK) {
        keyword = lhq.value;
        lt->token = keyword->token;
        lt->number = keyword->number;
        lexer->keyword = 1;
    }
}
