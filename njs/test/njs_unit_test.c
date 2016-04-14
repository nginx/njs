
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <nxt_types.h>
#include <nxt_clang.h>
#include <nxt_stub.h>
#include <nxt_malloc.h>
#include <nxt_array.h>
#include <nxt_lvlhsh.h>
#include <nxt_mem_cache_pool.h>
#include <njscript.h>
#include <string.h>
#include <stdio.h>
#include <sys/resource.h>
#include <time.h>


typedef struct {
    nxt_str_t  script;
    nxt_str_t  ret;
} njs_unit_test_t;


static njs_unit_test_t  njs_test[] =
{
    /* Variable declarations. */

    { nxt_string("var x"),
      nxt_string("undefined") },

    { nxt_string("var x;"),
      nxt_string("undefined") },

    { nxt_string("var x;;"),
      nxt_string("undefined") },

    { nxt_string("var x = 0"),
      nxt_string("undefined") },

    { nxt_string("var x = 0;"),
      nxt_string("undefined") },

    { nxt_string("var x = 0;;"),
      nxt_string("undefined") },

    { nxt_string("var; a"),
      nxt_string("SyntaxError") },

    { nxt_string("var \n a \n = 1; a"),
      nxt_string("1") },

    { nxt_string("var \n a, \n b; b"),
      nxt_string("undefined") },

    { nxt_string("var a = 1; var b; a"),
      nxt_string("1") },

    /* Numbers. */

    { nxt_string("999999999999999999999"),
      nxt_string("1e+21") },

#if 0
    { nxt_string("9223372036854775808"),
      nxt_string("9223372036854775808") },

    { nxt_string("18446744073709551616"),
      nxt_string("18446744073709552000") },

    { nxt_string("1.7976931348623157E+308"),
      nxt_string("1.7976931348623157e+308") },
#endif

    { nxt_string("+1"),
      nxt_string("1") },

    { nxt_string("+1\n"),
      nxt_string("1") },

    { nxt_string(""),
      nxt_string("undefined") },

    { nxt_string("\n"),
      nxt_string("undefined") },

    { nxt_string(";"),
      nxt_string("undefined") },

    { nxt_string("\n +1"),
      nxt_string("1") },

    /* An object "valueOf/toString" methods. */

    { nxt_string("var a = { valueOf: function() { return 1 } };    +a"),
      nxt_string("1") },

    { nxt_string("var a = { valueOf: function() { return '1' } };  +a"),
      nxt_string("1") },

    { nxt_string("var a = { valueOf: 2,"
                 "          toString: function() { return '1' } }; +a"),
      nxt_string("1") },

    { nxt_string("var a = { valueOf: function() { return [] },"
                 "          toString: function() { return '1' } }; +a"),
      nxt_string("1") },

    { nxt_string("var a = { toString: function() { return 'a' } };"
                 "var b = { toString: function() { return a+'b' } }; '0'+b"),
      nxt_string("0ab") },

    /**/

    { nxt_string("1 + undefined"),
      nxt_string("NaN") },

    { nxt_string("1 + ''"),
      nxt_string("1") },

    { nxt_string("undefined + undefined"),
      nxt_string("NaN") },

    { nxt_string("1.2 + 5.7"),
      nxt_string("6.9") },

    { nxt_string("1 + 1 + '2' + 1 + 1"),
      nxt_string("2211") },

    { nxt_string("1.2 - '5.7'"),
      nxt_string("-4.5") },

    { nxt_string("1.2 + -'5.7'"),
      nxt_string("-4.5") },

    { nxt_string("1 + +'3'"),
      nxt_string("4") },

    { nxt_string("1 - undefined"),
      nxt_string("NaN") },

    { nxt_string("1 - ''"),
      nxt_string("1") },

    { nxt_string("undefined - undefined"),
      nxt_string("NaN") },

    /* Assignment. */

    { nxt_string("var a, b = (a = [2]) * (3 * 4); a +' '+ b"),
      nxt_string("2 24") },

    /* 3 address operation and side effect. */

    { nxt_string("var a = 1; function f(x) { a = x; return 2 }; a+f(5)+' '+a"),
      nxt_string("3 5") },

    { nxt_string("var a = 1; function f(x) { a = x; return 2 }; a += f(5)"),
      nxt_string("3") },

    /**/

    { nxt_string("12 | 6"),
      nxt_string("14") },

    { nxt_string("12 | 'abc'"),
      nxt_string("12") },

    { nxt_string("-1 | 0"),
      nxt_string("-1") },

    { nxt_string("-2147483648 | 0"),
      nxt_string("-2147483648") },

    { nxt_string("1024.9 | 0"),
      nxt_string("1024") },

    { nxt_string("-1024.9 | 0"),
      nxt_string("-1024") },

    { nxt_string("9007199254740991 | 0"),
      nxt_string("-1") },

    { nxt_string("9007199254740992 | 0"),
      nxt_string("0") },

    { nxt_string("9007199254740993 | 0"),
      nxt_string("0") },

#if 0
    { nxt_string("9223372036854775808 | 0"),
      nxt_string("0") },
#endif

    { nxt_string("9223372036854777856 | 0"),
      nxt_string("2048") },

    { nxt_string("-9223372036854777856 | 0"),
      nxt_string("-2048") },

    { nxt_string("NaN | 0"),
      nxt_string("0") },

    { nxt_string("-NaN | 0"),
      nxt_string("0") },

    { nxt_string("Infinity | 0"),
      nxt_string("0") },

    { nxt_string("-Infinity | 0"),
      nxt_string("0") },

    { nxt_string("+0 | 0"),
      nxt_string("0") },

    { nxt_string("-0 | 0"),
      nxt_string("0") },

    { nxt_string("32.5 << 2.4"),
      nxt_string("128") },

    { nxt_string("32.5 << 'abc'"),
      nxt_string("32") },

    { nxt_string("'abc' << 2"),
      nxt_string("0") },

    { nxt_string("-1 << 0"),
      nxt_string("-1") },

    { nxt_string("-1 << -1"),
      nxt_string("-2147483648") },

    { nxt_string("-2147483648 << 0"),
      nxt_string("-2147483648") },

#if 0
    { nxt_string("9223372036854775808 << 0"),
      nxt_string("0") },
#endif

    { nxt_string("9223372036854777856 << 0"),
      nxt_string("2048") },

    { nxt_string("-9223372036854777856 << 0"),
      nxt_string("-2048") },

    { nxt_string("NaN << 0"),
      nxt_string("0") },

    { nxt_string("32.5 >> 2.4"),
      nxt_string("8") },

    { nxt_string("-1 >> 30"),
      nxt_string("-1") },

    { nxt_string("'abc' >> 2"),
      nxt_string("0") },

    { nxt_string("-1 >> 0"),
      nxt_string("-1") },

    { nxt_string("-1 >> -1"),
      nxt_string("-1") },

    { nxt_string("-2147483648 >> 0"),
      nxt_string("-2147483648") },

    { nxt_string("-2147483648 >> -1"),
      nxt_string("-1") },

#if 0
    { nxt_string("9223372036854775808 >> 0"),
      nxt_string("0") },
#endif

    { nxt_string("9223372036854777856 >> 0"),
      nxt_string("2048") },

    { nxt_string("-9223372036854777856 >> 0"),
      nxt_string("-2048") },

    { nxt_string("NaN >> 0"),
      nxt_string("0") },

    { nxt_string("-1 >>> 30"),
      nxt_string("3") },

    { nxt_string("NaN >>> 1"),
      nxt_string("0") },

#if 0
    { nxt_string("9223372036854775808 >>> 1"),
      nxt_string("0") },
#endif

    { nxt_string("-1 >>> 0"),
      nxt_string("4294967295") },

    { nxt_string("-1 >>> -1"),
      nxt_string("1") },

    { nxt_string("-2147483648 >>> 0"),
      nxt_string("2147483648") },

    { nxt_string("-2147483648 >>> -1"),
      nxt_string("1") },

#if 0
    { nxt_string("9223372036854775808 >>> 0"),
      nxt_string("0") },
#endif

    { nxt_string("9223372036854777856 >>> 0"),
      nxt_string("2048") },

    { nxt_string("-9223372036854777856 >>> 0"),
      nxt_string("4294965248") },

    { nxt_string("NaN >>> 0"),
      nxt_string("0") },

    { nxt_string("!2"),
      nxt_string("false") },

    /**/

    { nxt_string("var a = { valueOf: function() { return 1 } };   ~a"),
      nxt_string("-2") },

    { nxt_string("var a = { valueOf: function() { return '1' } }; ~a"),
      nxt_string("-2") },

    /**/

    { nxt_string("1 || 2"),
      nxt_string("1") },

    { nxt_string("var a = 1; 1 || (a = 2); a"),
      nxt_string("1") },

    { nxt_string("1 || 2 || 3"),
      nxt_string("1") },

    { nxt_string("1 || (2 + 2) || 3"),
      nxt_string("1") },

    { nxt_string("1 && 2"),
      nxt_string("2") },

    { nxt_string("1 && 2 && 3"),
      nxt_string("3") },

    { nxt_string("var a = 1; 0 && (a = 2); a"),
      nxt_string("1") },

    { nxt_string("false && true || true"),
      nxt_string("true") },

    { nxt_string("false && (true || true)"),
      nxt_string("false") },

    { nxt_string("a = true; a = -~!a"),
      nxt_string("1") },

    { nxt_string("12 & 6"),
      nxt_string("4") },

    { nxt_string("-1 & 65536"),
      nxt_string("65536") },

    { nxt_string("-2147483648 & 65536"),
      nxt_string("0") },

#if 0
    { nxt_string("9223372036854775808 & 65536"),
      nxt_string("0") },
#endif

    { nxt_string("NaN & 65536"),
      nxt_string("0") },

    { nxt_string("12 ^ 6"),
      nxt_string("10") },

    { nxt_string("-1 ^ 65536"),
      nxt_string("-65537") },

    { nxt_string("-2147483648 ^ 65536"),
      nxt_string("-2147418112") },

#if 0
    { nxt_string("9223372036854775808 ^ 65536"),
      nxt_string("65536") },
#endif

    { nxt_string("NaN ^ 65536"),
      nxt_string("65536") },

    { nxt_string("x = '1'; +x + 2"),
      nxt_string("3") },

    { nxt_string("'3' -+-+-+ '1' + '1' / '3' * '6' + '2'"),
      nxt_string("42") },

    { nxt_string("((+!![])+(+!![])+(+!![])+(+!![])+[])+((+!![])+(+!![])+[])"),
      nxt_string("42") },

    { nxt_string("1+[[]+[]]-[]+[[]-[]]-1"),
      nxt_string("9") },

    { nxt_string("[[]+[]]-[]+[[]-[]]"),
      nxt_string("00") },

    { nxt_string("'true' == true"),
      nxt_string("false") },

    { nxt_string("null == false"),
      nxt_string("false") },

    { nxt_string("!null"),
      nxt_string("true") },

    { nxt_string("0 === -0"),
      nxt_string("true") },

    { nxt_string("1/-0"),
      nxt_string("-Infinity") },

    { nxt_string("1/0 === 1/-0"),
      nxt_string("false") },

    { nxt_string("1 == true"),
      nxt_string("true") },

    { nxt_string("NaN === NaN"),
      nxt_string("false") },

    { nxt_string("NaN !== NaN"),
      nxt_string("true") },

    { nxt_string("NaN == NaN"),
      nxt_string("false") },

    { nxt_string("NaN != NaN"),
      nxt_string("true") },

    { nxt_string("NaN == false"),
      nxt_string("false") },

    { nxt_string("Infinity == Infinity"),
      nxt_string("true") },

    { nxt_string("-Infinity == -Infinity"),
      nxt_string("true") },

    { nxt_string("-Infinity < Infinity"),
      nxt_string("true") },

    { nxt_string("Infinity - Infinity"),
      nxt_string("NaN") },

    { nxt_string("Infinity - -Infinity"),
      nxt_string("Infinity") },

    { nxt_string("undefined == 0"),
      nxt_string("false") },

    { nxt_string("undefined == null"),
      nxt_string("true") },

    { nxt_string("'1' == 1"),
      nxt_string("true") },

    { nxt_string("'1a' == '1'"),
      nxt_string("false") },

    { nxt_string("'abc' == 'abc'"),
      nxt_string("true") },

    { nxt_string("'abc' < 'abcde'"),
      nxt_string("true") },

    { nxt_string("0 == ''"),
      nxt_string("true") },

    { nxt_string("0 == ' '"),
      nxt_string("true") },

    { nxt_string("0 == '  '"),
      nxt_string("true") },

    { nxt_string("0 == '0'"),
      nxt_string("true") },

    { nxt_string("0 == ' 0 '"),
      nxt_string("true") },

    { nxt_string("0 == '000'"),
      nxt_string("true") },

    { nxt_string("'0' == ''"),
      nxt_string("false") },

    { nxt_string("1 < 2"),
      nxt_string("true") },

    { nxt_string("NaN < NaN"),
      nxt_string("false") },

    { nxt_string("NaN > NaN"),
      nxt_string("false") },

    { nxt_string("undefined < 1"),
      nxt_string("false") },

    { nxt_string("[] == false"),
      nxt_string("true") },

    { nxt_string("[0] == false"),
      nxt_string("true") },

    { nxt_string("[0,0] == false"),
      nxt_string("false") },

    { nxt_string("({}) == false"),
      nxt_string("false") },

    { nxt_string("var a = { valueOf: function() { return 5 } };   a == 5"),
      nxt_string("true") },

    { nxt_string("var a = { valueOf: function() { return '5' } }; a == 5"),
      nxt_string("true") },

    { nxt_string("var a = { valueOf: function() { return '5' } }; a == '5'"),
      nxt_string("true") },

    /* Comparisions. */

    { nxt_string("1 < 2"),
      nxt_string("true") },

    { nxt_string("1 < 1"),
      nxt_string("false") },

    { nxt_string("1 <= 1"),
      nxt_string("true") },

    { nxt_string("1 <= 2"),
      nxt_string("true") },

    { nxt_string("2 > 1"),
      nxt_string("true") },

    { nxt_string("1 > 2"),
      nxt_string("false") },

    { nxt_string("1 > 1"),
      nxt_string("false") },

    { nxt_string("1 >= 1"),
      nxt_string("true") },

    { nxt_string("2 >= 1"),
      nxt_string("true") },

    { nxt_string("1 >= 2"),
      nxt_string("false") },

    /**/

    { nxt_string("null === null"),
      nxt_string("true") },

    { nxt_string("null !== null"),
      nxt_string("false") },

    { nxt_string("null == null"),
      nxt_string("true") },

    { nxt_string("null != null"),
      nxt_string("false") },

    { nxt_string("null < null"),
      nxt_string("false") },

    { nxt_string("null > null"),
      nxt_string("false") },

    { nxt_string("null <= null"),
      nxt_string("true") },

    { nxt_string("null >= null"),
      nxt_string("true") },

    /**/

    { nxt_string("null === undefined"),
      nxt_string("false") },

    { nxt_string("null !== undefined"),
      nxt_string("true") },

    { nxt_string("null == undefined"),
      nxt_string("true") },

    { nxt_string("null != undefined"),
      nxt_string("false") },

    { nxt_string("null < undefined"),
      nxt_string("false") },

    { nxt_string("null > undefined"),
      nxt_string("false") },

    { nxt_string("null <= undefined"),
      nxt_string("false") },

    { nxt_string("null >= undefined"),
      nxt_string("false") },

    /**/

    { nxt_string("null === false"),
      nxt_string("false") },

    { nxt_string("null !== false"),
      nxt_string("true") },

    { nxt_string("null == false"),
      nxt_string("false") },

    { nxt_string("null != false"),
      nxt_string("true") },

    { nxt_string("null < false"),
      nxt_string("false") },

    { nxt_string("null > false"),
      nxt_string("false") },

    { nxt_string("null <= false"),
      nxt_string("true") },

    { nxt_string("null >= false"),
      nxt_string("true") },

    /**/

    { nxt_string("null === true"),
      nxt_string("false") },

    { nxt_string("null !== true"),
      nxt_string("true") },

    { nxt_string("null == true"),
      nxt_string("false") },

    { nxt_string("null != true"),
      nxt_string("true") },

    { nxt_string("null < true"),
      nxt_string("true") },

    { nxt_string("null > true"),
      nxt_string("false") },

    { nxt_string("null <= true"),
      nxt_string("true") },

    { nxt_string("null >= true"),
      nxt_string("false") },

    /**/

    { nxt_string("Infinity === Infinity"),
      nxt_string("true") },

    { nxt_string("Infinity !== Infinity"),
      nxt_string("false") },

    { nxt_string("Infinity == Infinity"),
      nxt_string("true") },

    { nxt_string("Infinity != Infinity"),
      nxt_string("false") },

    { nxt_string("Infinity < Infinity"),
      nxt_string("false") },

    { nxt_string("Infinity > Infinity"),
      nxt_string("false") },

    { nxt_string("Infinity <= Infinity"),
      nxt_string("true") },

    { nxt_string("Infinity >= Infinity"),
      nxt_string("true") },

    /**/

    { nxt_string("-Infinity === Infinity"),
      nxt_string("false") },

    { nxt_string("-Infinity !== Infinity"),
      nxt_string("true") },

    { nxt_string("-Infinity == Infinity"),
      nxt_string("false") },

    { nxt_string("-Infinity != Infinity"),
      nxt_string("true") },

    { nxt_string("-Infinity < Infinity"),
      nxt_string("true") },

    { nxt_string("-Infinity > Infinity"),
      nxt_string("false") },

    { nxt_string("-Infinity <= Infinity"),
      nxt_string("true") },

    { nxt_string("-Infinity >= Infinity"),
      nxt_string("false") },

    /**/

    { nxt_string("NaN === NaN"),
      nxt_string("false") },

    { nxt_string("NaN !== NaN"),
      nxt_string("true") },

    { nxt_string("NaN == NaN"),
      nxt_string("false") },

    { nxt_string("NaN != NaN"),
      nxt_string("true") },

    { nxt_string("NaN < NaN"),
      nxt_string("false") },

    { nxt_string("NaN > NaN"),
      nxt_string("false") },

    { nxt_string("NaN >= NaN"),
      nxt_string("false") },

    { nxt_string("NaN <= NaN"),
      nxt_string("false") },

    /**/

    { nxt_string("null < 0"),
      nxt_string("false") },

    { nxt_string("null < 1"),
      nxt_string("true") },

    { nxt_string("null < NaN"),
      nxt_string("false") },

    { nxt_string("null < -Infinity"),
      nxt_string("false") },

    { nxt_string("null < Infinity"),
      nxt_string("true") },

    { nxt_string("null < 'null'"),
      nxt_string("false") },

    { nxt_string("null < '1'"),
      nxt_string("true") },

    { nxt_string("null < [1]"),
      nxt_string("true") },

    { nxt_string("null < ({})"),
      nxt_string("false") },

    { nxt_string("var a = { valueOf: function() { return 1 } }      null < a"),
      nxt_string("true") },

    { nxt_string("var a = { valueOf: function() { return 'null' } } null < a"),
      nxt_string("false") },

    { nxt_string("var a = { valueOf: function() { return '1' } }    null < a"),
      nxt_string("true") },

    /**/

    { nxt_string("undefined < null"),
      nxt_string("false") },

    { nxt_string("undefined < undefined"),
      nxt_string("false") },

    { nxt_string("undefined < false"),
      nxt_string("false") },

    { nxt_string("undefined < true"),
      nxt_string("false") },

    { nxt_string("undefined < 0"),
      nxt_string("false") },

    { nxt_string("undefined < 1"),
      nxt_string("false") },

    { nxt_string("undefined < NaN"),
      nxt_string("false") },

    { nxt_string("undefined < -Infinity"),
      nxt_string("false") },

    { nxt_string("undefined < Infinity"),
      nxt_string("false") },

    { nxt_string("undefined < 'undefined'"),
      nxt_string("false") },

    { nxt_string("undefined < '1'"),
      nxt_string("false") },

    { nxt_string("undefined < [1]"),
      nxt_string("false") },

    { nxt_string("undefined < ({})"),
      nxt_string("false") },

    { nxt_string("var a = { valueOf: function() { return 1 } } undefined < a"),
      nxt_string("false") },

    { nxt_string("var a = { valueOf: function() { return 'undefined' } }"
                 "undefined < a"),
      nxt_string("false") },

    { nxt_string("var a = { valueOf: function() { return '1' } }"
                 "undefined < a"),
      nxt_string("false") },

    /**/

    { nxt_string("false < 1"),
      nxt_string("true") },

    { nxt_string("true < 1"),
      nxt_string("false") },

    { nxt_string("-1 < 1"),
      nxt_string("true") },

    { nxt_string("-1 < '1'"),
      nxt_string("true") },

    { nxt_string("NaN < NaN"),
      nxt_string("false") },

    { nxt_string("-Infinity < Infinity"),
      nxt_string("true") },

    { nxt_string("Infinity < -Infinity"),
      nxt_string("false") },

    { nxt_string("1 < 'abc'"),
      nxt_string("false") },

    /**/

    { nxt_string("[] === []"),
      nxt_string("false") },

    { nxt_string("[] !== []"),
      nxt_string("true") },

    { nxt_string("[] == []"),
      nxt_string("false") },

    { nxt_string("[] != []"),
      nxt_string("true") },

    { nxt_string("[] < []"),
      nxt_string("false") },

    { nxt_string("[] > []"),
      nxt_string("false") },

    { nxt_string("[] >= []"),
      nxt_string("true") },

    { nxt_string("[] <= []"),
      nxt_string("true") },

    /**/

    { nxt_string("({}) === ({})"),
      nxt_string("false") },

    { nxt_string("({}) !== ({})"),
      nxt_string("true") },

    { nxt_string("({}) == ({})"),
      nxt_string("false") },

    { nxt_string("({}) != ({})"),
      nxt_string("true") },

    { nxt_string("({}) > ({})"),
      nxt_string("false") },

    { nxt_string("({}) <= ({})"),
      nxt_string("true") },

    { nxt_string("({}) >= ({})"),
      nxt_string("true") },

    /**/

    { nxt_string("[0] == ({})"),
      nxt_string("false") },

    { nxt_string("[0] != ({})"),
      nxt_string("true") },

    { nxt_string("[0] <= ({})"),
      nxt_string("true") },

    { nxt_string("[0] >= ({})"),
      nxt_string("false") },

    /**/

    { nxt_string("a = 1 ? 2 : 3"),
      nxt_string("2") },

    { nxt_string("a = 1 ? 2 : 3 ? 4 : 5"),
      nxt_string("2") },

    { nxt_string("a = 0 ? 2 : 3 ? 4 : 5"),
      nxt_string("4") },

    { nxt_string("0 ? 2 ? 3 : 4 : 5"),
      nxt_string("5") },

    { nxt_string("1 ? 2 ? 3 : 4 : 5"),
      nxt_string("3") },

    { nxt_string("1 ? 0 ? 3 : 4 : 5"),
      nxt_string("4") },

    { nxt_string("(1 ? 0 : 3) ? 4 : 5"),
      nxt_string("5") },

    { nxt_string("a = (1 + 2) ? 2 ? 3 + 4 : 5 : 6"),
      nxt_string("7") },

    { nxt_string("a = (1 ? 2 : 3) + 4"),
      nxt_string("6") },

    { nxt_string("a = 1 ? b = 2 + 4 : b = 3"),
      nxt_string("6") },

    { nxt_string("a = 1 ? [1,2] : []"),
      nxt_string("1,2") },

    /**/

    { nxt_string("var a = { valueOf: function() { return 1 } };   +a"),
      nxt_string("1") },

    { nxt_string("var a = { valueOf: function() { return '1' } }; +a"),
      nxt_string("1") },

    { nxt_string("var a = { valueOf: function() { return 1 } };   -a"),
      nxt_string("-1") },

    { nxt_string("var a = { valueOf: function() { return '1' } }; -a"),
      nxt_string("-1") },

    /* Increment. */

    { nxt_string("var a = 1;   ++a"),
      nxt_string("2") },

    { nxt_string("var a = '1'; ++a"),
      nxt_string("2") },

    { nxt_string("var a = [1]; ++a"),
      nxt_string("2") },

    { nxt_string("var a = {};  ++a"),
      nxt_string("NaN") },

    { nxt_string("var a = [1,2,3]; var b = 1; b = ++a[b]; b + ' '+ a"),
      nxt_string("3 1,3,3") },

    { nxt_string("var a = { valueOf: function() { return 1 } };"
                 "++a +' '+ a +' '+ typeof a"),
      nxt_string("2 2 number") },

    { nxt_string("var a = { valueOf: function() { return '1' } };"
                 "++a +' '+ a +' '+ typeof a"),
      nxt_string("2 2 number") },

    { nxt_string("var a = { valueOf: function() { return [1] } };"
                 "++a +' '+ a +' '+ typeof a"),
      nxt_string("NaN NaN number") },

    { nxt_string("var a = { valueOf: function() { return {} } };"
                 "++a +' '+ a +' '+ typeof a"),
      nxt_string("NaN NaN number") },

    /**/

    { nxt_string("var a = 1;   a = ++a"),
      nxt_string("2") },

    { nxt_string("var a = '1'; a = ++a"),
      nxt_string("2") },

    { nxt_string("var a = [1]; a = ++a"),
      nxt_string("2") },

    { nxt_string("var a = {};  a = ++a"),
      nxt_string("NaN") },

    { nxt_string("var a = { valueOf: function() { return 1 } }   a = ++a"),
      nxt_string("2") },

    { nxt_string("var a = { valueOf: function() { return '1' } } a = ++a"),
      nxt_string("2") },

    { nxt_string("var a = { valueOf: function() { return [1] } } a = ++a"),
      nxt_string("NaN") },

    { nxt_string("var a = { valueOf: function() { return {} } }  a = ++a"),
      nxt_string("NaN") },

    /**/

    { nxt_string("var a = 1;   var b = ++a; a +' '+ b"),
      nxt_string("2 2") },

    { nxt_string("var a = '1'; var b = ++a; a +' '+ b"),
      nxt_string("2 2") },

    { nxt_string("var a = [1]; var b = ++a; a +' '+ b"),
      nxt_string("2 2") },

    { nxt_string("var a = {};  var b = ++a; a +' '+ b"),
      nxt_string("NaN NaN") },

    { nxt_string("var a = { valueOf: function() { return 1 } }"
                 "var b = ++a; a +' '+ b"),
      nxt_string("2 2") },

    { nxt_string("var a = { valueOf: function() { return '1' } }"
                 "var b = ++a; a +' '+ b"),
      nxt_string("2 2") },

    { nxt_string("var a = { valueOf: function() { return [1] } }"
                 "var b = ++a; a +' '+ b"),
      nxt_string("NaN NaN") },

    { nxt_string("var a = { valueOf: function() { return {} } }"
                 "var b = ++a; a +' '+ b"),
      nxt_string("NaN NaN") },

    /* Post increment. */

    { nxt_string("var a = 1;   a++"),
      nxt_string("1") },

    { nxt_string("var a = '1'; a++"),
      nxt_string("1") },

    { nxt_string("var a = [1]; a++"),
      nxt_string("1") },

    { nxt_string("var a = {};  a++"),
      nxt_string("NaN") },

    { nxt_string("var a = { valueOf: function() { return 1 } }   a++"),
      nxt_string("1") },

    { nxt_string("var a = { valueOf: function() { return '1' } } a++"),
      nxt_string("1") },

    { nxt_string("var a = { valueOf: function() { return [1] } } a++"),
      nxt_string("NaN") },

    { nxt_string("var a = { valueOf: function() { return {} } }  a++"),
      nxt_string("NaN") },

    /**/

    { nxt_string("var a = 1;   a = a++"),
      nxt_string("1") },

    { nxt_string("var a = '1'; a = a++"),
      nxt_string("1") },

    { nxt_string("var a = [1]; a = a++"),
      nxt_string("1") },

    { nxt_string("var a = {};  a = a++"),
      nxt_string("NaN") },

    { nxt_string("var a = { valueOf: function() { return 1 } }   a = a++"),
      nxt_string("1") },

    { nxt_string("var a = { valueOf: function() { return '1' } } a = a++"),
      nxt_string("1") },

    { nxt_string("var a = { valueOf: function() { return [1] } } a = a++"),
      nxt_string("NaN") },

    { nxt_string("var a = { valueOf: function() { return {} } }  a = a++"),
      nxt_string("NaN") },

    /**/

    { nxt_string("var a = 1;   var b = a++; a +' '+ b"),
      nxt_string("2 1") },

    { nxt_string("var a = '1'; var b = a++; a +' '+ b"),
      nxt_string("2 1") },

    { nxt_string("var a = [1]; var b = a++; a +' '+ b"),
      nxt_string("2 1") },

    { nxt_string("var a = {};  var b = a++; a +' '+ b"),
      nxt_string("NaN NaN") },

    { nxt_string("var a = { valueOf: function() { return 1 } }"
                 "var b = a++; a +' '+ b"),
      nxt_string("2 1") },

    { nxt_string("var a = { valueOf: function() { return '1' } }"
                 "var b = a++; a +' '+ b"),
      nxt_string("2 1") },

    { nxt_string("var a = { valueOf: function() { return [1] } }"
                 "var b = a++; a +' '+ b"),
      nxt_string("NaN NaN") },

    { nxt_string("var a = { valueOf: function() { return {} } }"
                 "var b = a++; a +' '+ b"),
      nxt_string("NaN NaN") },

    /* Decrement. */

    { nxt_string("var a = 1;   --a"),
      nxt_string("0") },

    { nxt_string("var a = '1'; --a"),
      nxt_string("0") },

    { nxt_string("var a = [1]; --a"),
      nxt_string("0") },

    { nxt_string("var a = {};  --a"),
      nxt_string("NaN") },

    { nxt_string("var a = { valueOf: function() { return 1} };   --a"),
      nxt_string("0") },

    { nxt_string("var a = { valueOf: function() { return '1'} }; --a"),
      nxt_string("0") },

    { nxt_string("var a = { valueOf: function() { return [1]} }; --a"),
      nxt_string("NaN") },

    { nxt_string("var a = { valueOf: function() { return {} } }; --a"),
      nxt_string("NaN") },

    /**/

    { nxt_string("var a = 1;   a = --a"),
      nxt_string("0") },

    { nxt_string("var a = '1'; a = --a"),
      nxt_string("0") },

    { nxt_string("var a = [1]; a = --a"),
      nxt_string("0") },

    { nxt_string("var a = {};  a = --a"),
      nxt_string("NaN") },

    { nxt_string("var a = { valueOf: function() { return 1} }   a = --a"),
      nxt_string("0") },

    { nxt_string("var a = { valueOf: function() { return '1'} } a = --a"),
      nxt_string("0") },

    { nxt_string("var a = { valueOf: function() { return [1]} } a = --a"),
      nxt_string("NaN") },

    { nxt_string("var a = { valueOf: function() { return {} } } a = --a"),
      nxt_string("NaN") },

    /**/

    { nxt_string("var a = 1;   var b = --a; a +' '+ b"),
      nxt_string("0 0") },

    { nxt_string("var a = '1'; var b = --a; a +' '+ b"),
      nxt_string("0 0") },

    { nxt_string("var a = [1]; var b = --a; a +' '+ b"),
      nxt_string("0 0") },

    { nxt_string("var a = {};  var b = --a; a +' '+ b"),
      nxt_string("NaN NaN") },

    { nxt_string("var a = { valueOf: function() { return 1 } }"
                 "var b = --a; a +' '+ b"),
      nxt_string("0 0") },

    { nxt_string("var a = { valueOf: function() { return '1' } }"
                 "var b = --a; a +' '+ b"),
      nxt_string("0 0") },

    { nxt_string("var a = { valueOf: function() { return [1] } }"
                 "var b = --a; a +' '+ b"),
      nxt_string("NaN NaN") },

    { nxt_string("var a = { valueOf: function() { return {} } }"
                 "var b = --a; a +' '+ b"),
      nxt_string("NaN NaN") },

    /* Post decrement. */

    { nxt_string("var a = 1;   a--"),
      nxt_string("1") },

    { nxt_string("var a = '1'; a--"),
      nxt_string("1") },

    { nxt_string("var a = [1]; a--"),
      nxt_string("1") },

    { nxt_string("var a = {};  a--"),
      nxt_string("NaN") },

    { nxt_string("var a = { valueOf: function() { return 1 } }   a--"),
      nxt_string("1") },

    { nxt_string("var a = { valueOf: function() { return '1' } } a--"),
      nxt_string("1") },

    { nxt_string("var a = { valueOf: function() { return [1] } } a--"),
      nxt_string("NaN") },

    { nxt_string("var a = { valueOf: function() { return {} } }  a--"),
      nxt_string("NaN") },

    /**/

    { nxt_string("var a = 1;   a = a--"),
      nxt_string("1") },

    { nxt_string("var a = '1'; a = a--"),
      nxt_string("1") },

    { nxt_string("var a = [1]; a = a--"),
      nxt_string("1") },

    { nxt_string("var a = {};  a = a--"),
      nxt_string("NaN") },

    { nxt_string("var a = { valueOf: function() { return 1 } }   a = a--"),
      nxt_string("1") },

    { nxt_string("var a = { valueOf: function() { return '1' } } a = a--"),
      nxt_string("1") },

    { nxt_string("var a = { valueOf: function() { return [1] } } a = a--"),
      nxt_string("NaN") },

    { nxt_string("var a = { valueOf: function() { return {} } }  a = a--"),
      nxt_string("NaN") },

    /**/

    { nxt_string("var a = 1;   var b = a--; a +' '+ b"),
      nxt_string("0 1") },

    { nxt_string("var a = '1'; var b = a--; a +' '+ b"),
      nxt_string("0 1") },

    { nxt_string("var a = [1]; var b = a--; a +' '+ b"),
      nxt_string("0 1") },

    { nxt_string("var a = {};  var b = a--; a +' '+ b"),
      nxt_string("NaN NaN") },

    { nxt_string("var a = { valueOf: function() { return 1 } }"
                 "var b = a--; a +' '+ b"),
      nxt_string("0 1") },

    { nxt_string("var a = { valueOf: function() { return '1' } }"
                 "var b = a--; a +' '+ b"),
      nxt_string("0 1") },

    { nxt_string("var a = { valueOf: function() { return [1] } }"
                 "var b = a--; a +' '+ b"),
      nxt_string("NaN NaN") },

    { nxt_string("var a = { valueOf: function() { return {} } }"
                 "var b = a--; a +' '+ b"),
      nxt_string("NaN NaN") },

    /**/

    { nxt_string("a = 2; b = ++a + ++a; a + ' ' + b"),
      nxt_string("4 7") },

    { nxt_string("a = 2; b = a++ + a++; a + ' ' + b"),
      nxt_string("4 5") },

    { nxt_string("a = b = 7; a +' '+ b"),
      nxt_string("7 7") },

    { nxt_string("a = b = c = 5; a +' '+ b +' '+ c"),
      nxt_string("5 5 5") },

    { nxt_string("a = b = (c = 5) + 2; a +' '+ b +' '+ c"),
      nxt_string("7 7 5") },

    { nxt_string("1, 2 + 5, 3"),
      nxt_string("3") },

    { nxt_string("a = 1 /* YES */\n b = a + 2 \n \n + 1 \n + 3"),
      nxt_string("7") },

    { nxt_string("a = 1 // YES \n b = a + 2 \n \n + 1 \n + 3"),
      nxt_string("7") },

    { nxt_string("a = 0; ++ \n a"),
      nxt_string("1") },

    { nxt_string("a = 0; a \n ++"),
      nxt_string("SyntaxError") },

    { nxt_string("a = 1 ? 2 \n : 3"),
      nxt_string("2") },

    { nxt_string("a = 0 / 0; b = 1 / 0; c = -1 / 0; a +' '+ b +' '+ c"),
      nxt_string("NaN Infinity -Infinity") },

    { nxt_string("a = (b = 7) + 5; var c; a +' '+ b +' '+ c"),
      nxt_string("12 7 undefined") },

    { nxt_string("var a, b = 1, c; a +' '+ b +' '+ c"),
      nxt_string("undefined 1 undefined") },

    { nxt_string("var a = 1, b = a + 1; a +' '+ b"),
      nxt_string("1 2") },

    { nxt_string("a = a = 1"),
      nxt_string("1") },

    { nxt_string("var a = 1, \n b; a +' '+ b"),
      nxt_string("1 undefined") },

    { nxt_string("a = b + 1; var b; a +' '+ b"),
      nxt_string("NaN undefined") },

    { nxt_string("var a += 1"),
      nxt_string("SyntaxError") },

    { nxt_string("var a = a + 1"),
      nxt_string("undefined") },

    { nxt_string("a = b + 1; var b = 1; a +' '+ b"),
      nxt_string("NaN 1") },

    { nxt_string("(a) = 1"),
      nxt_string("1") },

    { nxt_string("a"),
      nxt_string("ReferenceError") },

    { nxt_string("a + a"),
      nxt_string("ReferenceError") },

    { nxt_string("a = b + 1"),
      nxt_string("ReferenceError") },

    { nxt_string("a = a + 1"),
      nxt_string("ReferenceError") },

    { nxt_string("a += 1"),
      nxt_string("ReferenceError") },

    { nxt_string("a += 1; var a = 2"),
      nxt_string("undefined") },

    { nxt_string("var a = 1"),
      nxt_string("undefined") },

    { nxt_string("var a = 1; a = (a = 2) + a"),
      nxt_string("4") },

    { nxt_string("var a = 1; a = a + (a = 2)"),
      nxt_string("3") },

    { nxt_string("var a = 1; a += (a = 2)"),
      nxt_string("3") },

    { nxt_string("var a = b = 1; a +' '+ b"),
      nxt_string("1 1") },

    { nxt_string("var a \n if (!a) a = 3; a"),
      nxt_string("3") },

    /* if. */

    { nxt_string("if (0);"),
      nxt_string("undefined") },

    { nxt_string("if (0) {}"),
      nxt_string("undefined") },

    { nxt_string("if (0);else;"),
      nxt_string("undefined") },

    { nxt_string("var a = 1; if (true); else a = 2; a"),
      nxt_string("1") },

    { nxt_string("var a = 1; if (false); else a = 2; a"),
      nxt_string("2") },

    { nxt_string("var a = 1; if (true) a = 2; else a = 3; a"),
      nxt_string("2") },

    { nxt_string("var a = 3; if (true) if (false); else a = 2; a"),
      nxt_string("2") },

    { nxt_string("var a = 3; if (true) if (false); else; a = 2; a"),
      nxt_string("2") },

    /* switch. */

    { nxt_string("switch"),
      nxt_string("SyntaxError") },

    { nxt_string("switch (1);"),
      nxt_string("SyntaxError") },

    { nxt_string("switch (1) {}"),
      nxt_string("undefined") },

    { nxt_string("switch (1) {default:}"),
      nxt_string("undefined") },

    { nxt_string("switch (1) {case 0:}"),
      nxt_string("undefined") },

    { nxt_string("switch (1) {default:;}"),
      nxt_string("undefined") },

    { nxt_string("switch (1) {case 0:;}"),
      nxt_string("undefined") },

    { nxt_string("var a = 'A'; switch (a) {"
                 "case 0: a += '0';"
                 "case 1: a += '1';"
                 "}; a"),
      nxt_string("A") },

    { nxt_string("var a = 'A'; switch (0) {"
                 "case 0: a += '0';"
                 "case 1: a += '1';"
                 "}; a"),
      nxt_string("A01") },

    { nxt_string("var a = 'A'; switch (0) {"
                 "case 0: a += '0'; break;"
                 "case 1: a += '1';"
                 "}; a"),
      nxt_string("A0") },

    { nxt_string("var a = 'A'; switch (1) {"
                 "case 0: a += '0';"
                 "case 1: a += '1';"
                 "}; a"),
      nxt_string("A1") },

    { nxt_string("var a = 'A'; switch (2) {"
                 "case 0: a += '0';"
                 "case 1: a += '1';"
                 "default: a += 'D';"
                 "}; a"),
      nxt_string("AD") },

    { nxt_string("var a = 'A'; switch (2) {"
                 "case 0: a += '0';"
                 "default: a += 'D';"
                 "case 1: a += '1';"
                 "}; a"),
      nxt_string("AD1") },

    { nxt_string("var a = 'A'; function f(x) { a += x; return 0 }"
                 "switch (a) {"
                 "case f(1):"
                 "default:"
                 "case f(2): a += 'D';"
                 "case f(3): a += 'T';"
                 "} a"),
      nxt_string("A123DT") },

    /* continue. */

    { nxt_string("continue"),
      nxt_string("SyntaxError") },

    { nxt_string("do continue while (false)"),
      nxt_string("SyntaxError") },

    { nxt_string("do continue; while (false)"),
      nxt_string("undefined") },

    { nxt_string("do { continue } while (false)"),
      nxt_string("undefined") },

    { nxt_string("var i = 0; do if (i++ > 9) continue; while (i < 100); i"),
      nxt_string("100") },

    { nxt_string("while (false) continue"),
      nxt_string("undefined") },

    { nxt_string("while (false) continue;"),
      nxt_string("undefined") },

    { nxt_string("while (false) { continue }"),
      nxt_string("undefined") },

    { nxt_string("var i = 0; while (i < 100) if (i++ > 9) continue; i"),
      nxt_string("100") },

    { nxt_string("for ( ;null; ) continue"),
      nxt_string("undefined") },

    { nxt_string("for ( ;null; ) continue;"),
      nxt_string("undefined") },

    { nxt_string("for ( ;null; ) { continue }"),
      nxt_string("undefined") },

    { nxt_string("for (i = 0; i < 100; i++) if (i > 9) continue; i"),
      nxt_string("100") },

    { nxt_string("var a = []; for (i in a) continue"),
      nxt_string("undefined") },

    { nxt_string("var a = []; for (i in a) continue;"),
      nxt_string("undefined") },

    { nxt_string("var a = []; for (i in a) { continue }"),
      nxt_string("undefined") },

    { nxt_string("var a = [1,2,3,4,5]; var s = 0;"
                 "for (i in a) { if (a[i] > 4) continue; else s += a[i] } s"),
      nxt_string("10") },

    { nxt_string("var a = [1,2,3,4,5]; var s = 0;"
                 "for (i in a) { if (a[i] > 4) continue; s += a[i] } s"),
      nxt_string("10") },

    /* break. */

    { nxt_string("break"),
      nxt_string("SyntaxError") },

    { nxt_string("do break while (true)"),
      nxt_string("SyntaxError") },

    { nxt_string("do break; while (true)"),
      nxt_string("undefined") },

    { nxt_string("do { break } while (true)"),
      nxt_string("undefined") },

    { nxt_string("var i = 0; do if (i++ > 9) break; while (i < 100); i"),
      nxt_string("11") },

    { nxt_string("while (true) break"),
      nxt_string("undefined") },

    { nxt_string("while (true) break;"),
      nxt_string("undefined") },

    { nxt_string("while (true) { break }"),
      nxt_string("undefined") },

    { nxt_string("var i = 0; while (i < 100) if (i++ > 9) break; i"),
      nxt_string("11") },

    { nxt_string("for ( ;; ) break"),
      nxt_string("undefined") },

    { nxt_string("for ( ;; ) break;"),
      nxt_string("undefined") },

    { nxt_string("for ( ;; ) { break }"),
      nxt_string("undefined") },

    { nxt_string("for (i = 0; i < 100; i++) if (i > 9) break; i"),
      nxt_string("10") },

    { nxt_string("var a = []; for (i in a) break"),
      nxt_string("undefined") },

    { nxt_string("var a = []; for (i in a) break;"),
      nxt_string("undefined") },

    { nxt_string("var a = []; for (i in a) { break }"),
      nxt_string("undefined") },

    { nxt_string("var a = [1,2,3,4,5]; var s = 0;"
                 "for (i in a) { if (a[i] > 4) break; else s += a[i] } s"),
      nxt_string("10") },

    { nxt_string("var a = [1,2,3,4,5]; var s = 0;"
                 "for (i in a) { if (a[i] > 4) break; s += a[i] } s"),
      nxt_string("10") },

    { nxt_string("var a = [1,2,3,4,5]; var s = 0;"
                 "for (i in a) if (a[i] > 4) break; s += a[i] } s"),
      nxt_string("5") },

    /**/

    { nxt_string("for (i = 0; i < 10; i++) { i += 1 } i"),
      nxt_string("10") },

    /* Factorial. */

    { nxt_string("n = 5; f = 1; while (n--) f *= n + 1; f"),
      nxt_string("120") },

    { nxt_string("n = 5; f = 1; while (n) { f *= n; n-- } f"),
      nxt_string("120") },

    /* Fibonacci. */

    { nxt_string("var n = 50, x;"
                 "for(i=0,j=1,k=0; k<n; i=j,j=x,k++ ){ x=i+j } x"),
      nxt_string("20365011074") },

    { nxt_string("3 + 'abc' + 'def' + null + true + false + undefined"),
      nxt_string("3abcdefnulltruefalseundefined") },

    { nxt_string("a = 0; do a++; while (a < 5) if (a == 5) a = 7.33 \n"
                 "else a = 8; while (a < 10) a++; a"),
      nxt_string("10.33") },

    /* typeof. */

    { nxt_string("typeof null"),
      nxt_string("object") },

    { nxt_string("typeof undefined"),
      nxt_string("undefined") },

    { nxt_string("typeof false"),
      nxt_string("boolean") },

    { nxt_string("typeof true"),
      nxt_string("boolean") },

    { nxt_string("typeof 0"),
      nxt_string("number") },

    { nxt_string("typeof -1"),
      nxt_string("number") },

    { nxt_string("typeof Infinity"),
      nxt_string("number") },

    { nxt_string("typeof NaN"),
      nxt_string("number") },

    { nxt_string("typeof 'a'"),
      nxt_string("string") },

    { nxt_string("typeof {}"),
      nxt_string("object") },

    { nxt_string("typeof Object()"),
      nxt_string("object") },

    { nxt_string("typeof []"),
      nxt_string("object") },

    { nxt_string("typeof function(){}"),
      nxt_string("function") },

    { nxt_string("typeof Object"),
      nxt_string("function") },

    { nxt_string("typeof /./i"),
      nxt_string("object") },

    { nxt_string("typeof a"),
      nxt_string("undefined") },

    { nxt_string("typeof a; a"),
      nxt_string("ReferenceError") },

    { nxt_string("typeof a; a = 1"),
      nxt_string("1") },

    /**/

    { nxt_string("void 0"),
      nxt_string("undefined") },

    { nxt_string("undefined = 1"),
      nxt_string("SyntaxError") },

    { nxt_string("var a; b = a; a = 1; a +' '+ b"),
      nxt_string("1 undefined") },

    { nxt_string("a = 1"),
      nxt_string("1") },

    { nxt_string("a; a = 1; a"),
      nxt_string("ReferenceError") },

    { nxt_string("a = {}; typeof a +' '+ a"),
      nxt_string("object [object Object]") },

    { nxt_string("a = {}; a.b"),
      nxt_string("undefined") },

    { nxt_string("a = {}; a.b = 1 + 2; a.b"),
      nxt_string("3") },

    { nxt_string("a = {}; a['b']"),
      nxt_string("undefined") },

    { nxt_string("a = {}; a.b.c"),
      nxt_string("TypeError") },

    { nxt_string("a = {}; a.b = 1; a.b"),
      nxt_string("1") },

    { nxt_string("a = {}; a.b = 1; a.b += 2"),
      nxt_string("3") },

    { nxt_string("a = {}; a.b = 1; a.b += a.b"),
      nxt_string("2") },

    { nxt_string("a = {}; a.b = 1; x = {}; x.b = 3; a.b += (x.b = 2)"),
      nxt_string("3") },

    { nxt_string("a = {}; a.b = 1; a.b += (a.b = 2)"),
      nxt_string("3") },

    { nxt_string("a = {}; a.b += 1"),
      nxt_string("NaN") },

    { nxt_string("a = 1; b = 2; a = b += 1"),
      nxt_string("3") },

    { nxt_string("a = 1; b = { x:2 }; a = b.x += 1"),
      nxt_string("3") },

    { nxt_string("a = 1; b = { x:2 }; a = b.x += (a = 1)"),
      nxt_string("3") },

    { nxt_string("a = undefined; a.b++; a.b"),
      nxt_string("TypeError") },

    { nxt_string("a = null; a.b++; a.b"),
      nxt_string("TypeError") },

    { nxt_string("a = true; a.b++; a.b"),
      nxt_string("undefined") },

    { nxt_string("a = 1; a.b++; a.b"),
      nxt_string("undefined") },

    { nxt_string("a = {}; a.b = {}; a.b.c = 1; a.b['c']"),
      nxt_string("1") },

    { nxt_string("a = {}; a.b = {}; a.b.c = 1; a['b']['c']"),
      nxt_string("1") },

    { nxt_string("a = {}; a.b = {}; c = 'd'; a.b.d = 1; a['b'][c]"),
      nxt_string("1") },

    { nxt_string("a = {}; a.b = 1; c = a.b++; a.b +' '+ c"),
      nxt_string("2 1") },

    { nxt_string("a = 2; a.b = 1; c = a.b++; a +' '+ a.b +' '+ c"),
      nxt_string("2 undefined NaN") },

    { nxt_string("x = { a: 1 }; x.a"),
      nxt_string("1") },

    { nxt_string("a = { x:1 }; b = { y:2 }; a.x = b.y; a.x"),
      nxt_string("2") },

    { nxt_string("a = { x:1 }; b = { y:2 }; c = a.x = b.y; c"),
      nxt_string("2") },

    { nxt_string("a = { x:1 }; b = { y:2 }; a.x = b.y"),
      nxt_string("2") },

    { nxt_string("a = { x:1 }; b = a.x = 1 + 2; a.x +' '+ b"),
      nxt_string("3 3") },

    { nxt_string("a = { x:1 }; b = { y:2 }; c = {}; c.x = a.x = b.y; c.x"),
      nxt_string("2") },

    { nxt_string("y = 2; x = { a:1, b: y + 5, c:3 }; x.a +' '+ x.b +' '+ x.c"),
      nxt_string("1 7 3") },

    { nxt_string("x = { a: 1, b: { a:2, c:5 } }; x.a +' '+ x.b.a +' '+ x.b.c"),
      nxt_string("1 2 5") },

    { nxt_string("var y = 5; x = { a:y }; x.a"),
      nxt_string("5") },

    { nxt_string("x = { a: 1, b: x.a }"),
      nxt_string("ReferenceError") },

    { nxt_string("a = { b: 2 }; a.b += 1"),
      nxt_string("3") },

    { nxt_string("o = {a:1}; c = o; o.a = o = {b:5};"
                 "o.a +' '+ o.b +' '+ c.a.b"),
      nxt_string("undefined 5 5") },

    { nxt_string("y = { a: 2 }; x = { a: 1, b: y.a }; x.a +' '+ x.b"),
      nxt_string("1 2") },

    { nxt_string("y = { a: 1 }; x = { a: y.a++, b: y.a++ }\n"
                 "x.a +' '+ x.b +' '+ y.a"),
      nxt_string("1 2 3") },

    { nxt_string("var a=''; var o = {a:1, b:2}"
                 "for (p in o) { a += p +':'+ o[p] +',' } a"),
      nxt_string("a:1,b:2,") },

    { nxt_string("x = { a: 1 }; b = delete x.a; x.a +' '+ b"),
      nxt_string("undefined true") },

    { nxt_string("delete null"),
      nxt_string("true") },

    { nxt_string("var a; delete a"),
      nxt_string("false") },

    { nxt_string("delete undefined"),
      nxt_string("false") },

    { nxt_string("delete NaN"),
      nxt_string("false") },

    { nxt_string("delete Infinity"),
      nxt_string("false") },

    { nxt_string("delete -Infinity"),
      nxt_string("true") },

    { nxt_string("delete (1/0)"),
      nxt_string("true") },

    { nxt_string("delete 1"),
      nxt_string("true") },

    { nxt_string("delete (a = 1); a"),
      nxt_string("1") },

    { nxt_string("delete a"),
      nxt_string("true") },

    { nxt_string("a = 1; delete a"),
      nxt_string("true") },

    { nxt_string("a = 1; delete a; typeof a"),
      nxt_string("undefined") },

    { nxt_string("a = { x:1 }; ('x' in a) +' '+ (1 in a)"),
      nxt_string("true false") },

    { nxt_string("a = {}; 1 in a"),
      nxt_string("false") },

    { nxt_string("a = 1; 1 in a"),
      nxt_string("TypeError") },

    { nxt_string("a = true; 1 in a"),
      nxt_string("TypeError") },

    { nxt_string("var n = { toString: function() { return 'a' } };"
                 "var o = { a: 5 }; o[n]"),
      nxt_string("5") },

    { nxt_string("var n = { valueOf: function() { return 'a' } };"
                 "var o = { a: 5, '[object Object]': 7 }; o[n]"),
      nxt_string("7") },

    /* Arrays */

    /* Empty array to primitive. */

    { nxt_string("3 + []"),
      nxt_string("3") },

    { nxt_string("3 * []"),
      nxt_string("0") },

    /* Single element array to primitive. */

    { nxt_string("3 + [5]"),
      nxt_string("35") },

    { nxt_string("3 * [5]"),
      nxt_string("15") },

    /* Array to primitive. */

    { nxt_string("3 + [5,7]"),
      nxt_string("35,7") },

    { nxt_string("3 * [5,7]"),
      nxt_string("NaN") },


    { nxt_string("a = [ 1, 2, 3 ]; a[0] + a[1] + a[2]"),
      nxt_string("6") },

    { nxt_string("a = [ 1, 2, 3 ]; a[0] +' '+ a[1] +' '+ a[2] +' '+ a[3]"),
      nxt_string("1 2 3 undefined") },

    { nxt_string("[] - 2"),
      nxt_string("-2") },

    { nxt_string("[1] - 2"),
      nxt_string("-1") },

    { nxt_string("[[1]] - 2"),
      nxt_string("-1") },

    { nxt_string("[[[1]]] - 2"),
      nxt_string("-1") },

    { nxt_string("a = []; a - 2"),
      nxt_string("-2") },

    { nxt_string("a = [1]; a - 2"),
      nxt_string("-1") },

    { nxt_string("a = []; a[0] = 1; a - 2"),
      nxt_string("-1") },

    { nxt_string("[] + 2 + 3"),
      nxt_string("23") },

    { nxt_string("[1] + 2 + 3"),
      nxt_string("123") },

    { nxt_string("a = []; a + 2 + 3"),
      nxt_string("23") },

    { nxt_string("a = [1]; a + 2 + 3"),
      nxt_string("123") },

    { nxt_string("a = [1,2]; i = 0; a[i++] += a[0] = 5 + i; a[0] +' '+ a[1]"),
      nxt_string("7 2") },

    { nxt_string("a = []; a[0] = 1; a + 2 + 3"),
      nxt_string("123") },

    { nxt_string("a = []; a['0'] = 1; a + 2 + 3"),
      nxt_string("123") },

    { nxt_string("a = []; a[2] = 1; a[2]"),
      nxt_string("1") },

    { nxt_string("a = [1, 2]; 1 in a"),
      nxt_string("true") },

    { nxt_string("a = [1, 2]; 2 in a"),
      nxt_string("false") },

    { nxt_string("a = [1, 2]; delete a[0]; 0 in a"),
      nxt_string("false") },

    { nxt_string("var s = '', a = [5,1,2];"
                 "a[null] = null;"
                 "a[undefined] = 'defined';"
                 "a[false] = false;"
                 "a[true] = true;"
                 "a[-0] = 0;"
                 "a[Infinity] = Infinity;"
                 "a[-Infinity] = -Infinity;"
                 "a[NaN] = NaN;"
                 "a[-NaN] = -NaN;"
                 "for (i in a) { s += i +':'+ a[i] +',' } s"),
      nxt_string("0:0,1:1,2:2,null:null,undefined:defined,false:false,"
                 "true:true,Infinity:Infinity,-Infinity:-Infinity,NaN:NaN,") },

    { nxt_string("[].length"),
      nxt_string("0") },

    { nxt_string("[1,2].length"),
      nxt_string("2") },

    { nxt_string("a = [1,2]; a.length"),
      nxt_string("2") },

    { nxt_string("a = [1,2,3]; a.join()"),
      nxt_string("1,2,3") },

    { nxt_string("a = [1,2,3]; a.join(':')"),
      nxt_string("1:2:3") },

    { nxt_string("a = []; a[5] = 5; a.join()"),
      nxt_string(",,,,,5") },

    { nxt_string("a = []; a[5] = 5; a"),
      nxt_string(",,,,,5") },

    { nxt_string("a = []; a.concat([])"),
      nxt_string("") },

    { nxt_string("var s = { toString: function() { return 'S' } }"
                 "var v = { toString: 8, valueOf: function() { return 'V' } }"
                 "var o = [9]; o.join = function() { return 'O' };"
                 "var a = [1,2,3,[4,5,6],s,v,o]; a.join('')"),
      nxt_string("1234,5,6SVO") },

    { nxt_string("var s = { toString: function() { return 'S' } }"
                 "var v = { toString: 8, valueOf: function() { return 'V' } }"
                 "var o = [9]; o.join = function() { return 'O' };"
                 "var a = [1,2,3,[4,5,6],s,v,o]; a"),
      nxt_string("1,2,3,4,5,6,S,V,O") },

    /* Array.toString(). */

    { nxt_string("a = [1,2,3]; a.join = 'NO';"
                 "Object.prototype.toString = function () { return 'A' }; a"),
      nxt_string("[object Array]") },

    { nxt_string("Array.prototype.toString.call(1)"),
      nxt_string("[object Number]") },

    { nxt_string("Array.prototype.toString.call('abc')"),
      nxt_string("[object String]") },

    /* Empty array elements. */

    { nxt_string("[,,]"),
      nxt_string(",") },

    { nxt_string("[,,,]"),
      nxt_string(",,") },

    { nxt_string("[1,2,]"),
      nxt_string("1,2") },

    { nxt_string("[1,2,,3]"),
      nxt_string("1,2,,3") },

    { nxt_string("[,,].length"),
      nxt_string("2") },

    { nxt_string("[,,,].length"),
      nxt_string("3") },

    { nxt_string("[1,2,,3].length"),
      nxt_string("4") },

    /**/

    { nxt_string("var n = { toString: function() { return 1 } };   [1,2][n]"),
      nxt_string("2") },

    { nxt_string("var n = { toString: function() { return '1' } }; [1,2][n]"),
      nxt_string("2") },

    { nxt_string("var n = { toString: function() { return 1 },"
                          " valueOf:  function() { return 0 } };   [1,2][n]"),
      nxt_string("2") },

    { nxt_string("var n = { toString: function() { return 1.5 } };"
                 "var a = [1,2]; a[1.5] = 5; a[n]"),
      nxt_string("5") },

    { nxt_string("var n = { toString: function() { return 1.5 } };"
                 "var a = [1,2]; a[n] = 5; a[1.5]"),
      nxt_string("5") },

    { nxt_string("var n = { toString: function() { return '1.5' } };"
                 "var a = [1,2]; a[1.5] = 5; a[n]"),
      nxt_string("5") },

    { nxt_string("var n = { toString: function() { return '1.5' } };"
                 "var a = [1,2]; a[n] = 5; a[1.5]"),
      nxt_string("5") },

    { nxt_string("var n = { toString: function() { return 1.5 } };"
                 "var a = [1,2]; a[1.5] = 5; n in a"),
      nxt_string("true") },

    { nxt_string("var n = { toString: function() { return '1.5' } };"
                 "var a = [1,2]; a[1.5] = 5; '' + (n in a) + (delete a[n])"),
      nxt_string("truetrue") },

    /**/

    { nxt_string("a = [1,2,3]; a.concat(4, [5, 6, 7], 8)"),
      nxt_string("1,2,3,4,5,6,7,8") },

    { nxt_string("a = []; a[100] = a.length; a[100] +' '+ a.length"),
      nxt_string("0 101") },

    { nxt_string("a = [1,2]; a[100] = 100; a[100] +' '+ a.length"),
      nxt_string("100 101") },

    { nxt_string("a = [1,2,3,4,5]; b = a.slice(3); b[0] +' '+ b[1] +' '+ b[2]"),
      nxt_string("4 5 undefined") },

    { nxt_string("a = [1,2]; a.pop() +' '+ a.length +' '+ a"),
      nxt_string("2 1 1") },

    { nxt_string("a = [1,2]; len = a.push(3); len +' '+ a.pop() +' '+ a"),
      nxt_string("3 3 1,2") },

    { nxt_string("a = [1,2]; len = a.push(3,4,5); len +' '+ a.pop() +' '+ a"),
      nxt_string("5 5 1,2,3,4") },

    { nxt_string("a = [1,2,3]; a.shift() +' '+ a[0] +' '+ a.length"),
      nxt_string("1 2 2") },

    { nxt_string("a = [1,2]; len = a.unshift(3);"
                 "len +' '+ a +' '+ a.shift()"),
      nxt_string("3 3,1,2 3") },

    { nxt_string("a = [1,2]; len = a.unshift(3,4,5);"
                 "len +' '+ a +' '+ a.shift()"),
      nxt_string("5 3,4,5,1,2 3") },

    { nxt_string("var a = []; var s = { sum: 0 };"
                 "a.forEach(function(v, i, a) { this.sum += v }, s); s.sum"),
      nxt_string("0") },

    { nxt_string("var a = new Array(3); var s = { sum: 0 };"
                 "a.forEach(function(v, i, a) { this.sum += v }, s); s.sum"),
      nxt_string("0") },

    { nxt_string("var a = [,,,]; var s = { sum: 0 };"
                 "a.forEach(function(v, i, a) { this.sum += v }, s); s.sum"),
      nxt_string("0") },

    { nxt_string("var a = [1,2,3]; var s = { sum: 0 };"
                 "a.forEach(function(v, i, a) { this.sum += v }, s); s.sum"),
      nxt_string("6") },

    { nxt_string("var a = [1,2,3];"
                 "a.forEach(function(v, i, a) { a[i+3] = a.length }); a"),
      nxt_string("1,2,3,3,4,5") },

    { nxt_string("var a = [1,2,3]; var s = { sum: 0 };"
                 "[].forEach.call(a, function(v, i, a) { this.sum += v }, s);"
                 "s.sum"),
      nxt_string("6") },

    { nxt_string("var a = [1,2,3]; var s = { sum: 0 };"
                 "[].forEach.apply(a,"
                                  "[ function(v, i, a) { this.sum += v }, s ]);"
                 "s.sum"),
      nxt_string("6") },

    { nxt_string("var a = [];"
                 "a.some(function(v, i, a) { return v > 1 })"),
      nxt_string("false") },

    { nxt_string("var a = [1,2,3];"
                 "a.some(function(v, i, a) { return v > 1 })"),
      nxt_string("true") },

    { nxt_string("var a = [1,2,3];"
                 "a.some(function(v, i, a) { return v > 2 })"),
      nxt_string("true") },

    { nxt_string("var a = [1,2,3];"
                 "a.some(function(v, i, a) { return v > 3 })"),
      nxt_string("false") },

    { nxt_string("var a = [];"
                 "a.every(function(v, i, a) { return v > 1 })"),
      nxt_string("true") },

    { nxt_string("var a = [3,2,1];"
                 "a.every(function(v, i, a) { return v > 3 })"),
      nxt_string("false") },

    { nxt_string("var a = [3,2,1];"
                 "a.every(function(v, i, a) { return v > 2 })"),
      nxt_string("false") },

    { nxt_string("var a = [3,2,1];"
                 "a.every(function(v, i, a) { return v > 0 })"),
      nxt_string("true") },

    /* Strings. */

    { nxt_string("var a = '0123456789' + '012345'"
                 "var b = 'abcdefghij' + 'klmnop'"
                 "    a = b"),
      nxt_string("abcdefghijklmnop") },

    /* Escape strings. */

    { nxt_string("'\\a \\' \\\" \\\\ \\0 \\b \\f \\n \\r \\t \\v'"),
      nxt_string("a ' \" \\ \0 \b \f \n \r \t \v") },

    { nxt_string("'a\\\nb'"),
      nxt_string("ab") },

    { nxt_string("'a\\\rb'"),
      nxt_string("ab") },

    { nxt_string("'a\\\r\nb'"),
      nxt_string("ab") },

    { nxt_string("'\\'"),
      nxt_string("SyntaxError") },

    { nxt_string("'\\u03B1'"),
      nxt_string("α") },

    { nxt_string("'\\u'"),
      nxt_string("SyntaxError") },

    { nxt_string("'\\u03B'"),
      nxt_string("SyntaxError") },

    { nxt_string("'\\u{61}\\u{3B1}\\u{20AC}'"),
      nxt_string("aα€") },

    { nxt_string("'\\u'"),
      nxt_string("SyntaxError") },

    { nxt_string("'\\u{'"),
      nxt_string("SyntaxError") },

    { nxt_string("'\\u{}'"),
      nxt_string("SyntaxError") },

    { nxt_string("'\\u{1234567}'"),
      nxt_string("SyntaxError") },

    { nxt_string("'\\x61'"),
      nxt_string("a") },

    { nxt_string("''.length"),
      nxt_string("0") },

    { nxt_string("'abc'.length"),
      nxt_string("3") },

    { nxt_string("'abc'.toUTF8().length"),
      nxt_string("3") },

    { nxt_string("'абв'.length"),
      nxt_string("3") },

    { nxt_string("'абв'.toUTF8().length"),
      nxt_string("6") },

    { nxt_string("'αβγ'.length"),
      nxt_string("3") },

    { nxt_string("'αβγ'.toUTF8().length"),
      nxt_string("6") },

    { nxt_string("'絵文字'.length"),
      nxt_string("3") },

    { nxt_string("'絵文字'.toUTF8().length"),
      nxt_string("9") },

    { nxt_string("'えもじ'.length"),
      nxt_string("3") },

    { nxt_string("'えもじ'.toUTF8().length"),
      nxt_string("9") },

    { nxt_string("'囲碁織'.length"),
      nxt_string("3") },

    { nxt_string("'囲碁織'.toUTF8().length"),
      nxt_string("9") },

    { nxt_string("a = 'abc'; a.length"),
      nxt_string("3") },

    { nxt_string("a = 'abc'; a['length']"),
      nxt_string("3") },

    { nxt_string("a = 'абв'; a.length"),
      nxt_string("3") },

    { nxt_string("a = 'abc' + 'абв'; a.length"),
      nxt_string("6") },

    { nxt_string("a = 'abc' + 1 + 'абв'; a +' '+ a.length"),
      nxt_string("abc1абв 7") },

    { nxt_string("a = 1; a.length"),
      nxt_string("undefined") },

    { nxt_string("a = 'abc'; a.concat('абв', 123)"),
      nxt_string("abcабв123") },

    { nxt_string("''.concat.call(0, 1, 2, 3, 4, 5, 6, 7, 8, 9)"),
      nxt_string("0123456789") },

    { nxt_string("''.concat.apply(0, [1, 2, 3, 4, 5, 6, 7, 8, 9])"),
      nxt_string("0123456789") },

    { nxt_string("var f = ''.concat.bind(0, 1, 2, 3, 4); f(5, 6, 7, 8, 9)"),
      nxt_string("0123456789") },

    { nxt_string("var f = String.prototype.concat.bind(0, 1); f(2)"),
      nxt_string("012") },

    { nxt_string("var f = Function.prototype.call.bind"
                 "                            (String.prototype.concat, 0, 1);"
                 "f(2)"),
      nxt_string("012") },

    { nxt_string("var f = String.prototype.concat.bind(0, 1);"
                 "var o = { toString: f }; o"),
      nxt_string("01") },

    { nxt_string("''.concat.bind(0, 1, 2, 3, 4).call(5, 6, 7, 8, 9)"),
      nxt_string("012346789") },

    { nxt_string("''.concat.bind(0, 1, 2, 3, 4).apply(5,[6, 7, 8, 9])"),
      nxt_string("012346789") },

    { nxt_string("var f = Array.prototype.join.bind([0, 1, 2]); f()"),
      nxt_string("0,1,2") },

    { nxt_string("var f = Array.prototype.join.bind([0, 1, 2]);"
                 "var o = { toString: f }; o"),
      nxt_string("0,1,2") },

    { nxt_string("var f = Array.prototype.join.bind([0, 1, 2]); f('.')"),
      nxt_string("0.1.2") },

    { nxt_string("var f = Array.prototype.join.bind([0, 1, 2], '.');"
                 "var o = { toString: f }; o"),
      nxt_string("0.1.2") },

    { nxt_string("var f = Array.prototype.toString.bind([0, 1, 2]); f()"),
      nxt_string("0,1,2") },

    { nxt_string("var f = Array.prototype.toString.bind([0, 1, 2]);"
                 "var o = { toString: f }; o"),
      nxt_string("0,1,2") },

    { nxt_string("var s = { toString: function() { return '123' } };"
                 "var a = 'abc'; a.concat('абв', s)"),
      nxt_string("abcабв123") },

    { nxt_string("'\\u00CE\\u00B1'.toBytes() == 'α'"),
      nxt_string("true") },

    { nxt_string("'\\u00CE\\u00B1'.toBytes() === 'α'"),
      nxt_string("false") },

    { nxt_string("b = '\\u00C2\\u00B6'.toBytes(); u = b.fromUTF8();"
                 "b.length +' '+ b +' '+ u.length +' '+ u"),
      nxt_string("2 ¶ 1 ¶") },

    { nxt_string("'α'.toBytes()"),
      nxt_string("null") },

    { nxt_string("'α'.toUTF8()[0]"),
      nxt_string("\xCE") },

    { nxt_string("a = 'a'.toBytes() + 'α'; a + a.length"),
      nxt_string("aα3") },

    { nxt_string("a = 'µ§±®'.toBytes(); a"),
      nxt_string("\xB5\xA7\xB1\xAE") },

    { nxt_string("a = 'µ§±®'.toBytes(2); a"),
      nxt_string("\xB1\xAE") },

    { nxt_string("a = 'µ§±®'.toBytes(1,3); a"),
      nxt_string("\xA7\xB1") },

    { nxt_string("a = '\\xB5\\xA7\\xB1\\xAE'.toBytes(); a.fromBytes()"),
      nxt_string("µ§±®") },

    { nxt_string("a = '\\xB5\\xA7\\xB1\\xAE'.toBytes(); a.fromBytes(2)"),
      nxt_string("±®") },

    { nxt_string("a = '\\xB5\\xA7\\xB1\\xAE'.toBytes(); a.fromBytes(1, 3)"),
      nxt_string("§±") },

    { nxt_string("a = 'abcdefgh'; a.substr(3, 15)"),
      nxt_string("defgh") },

    { nxt_string("'abcdefgh'.substr(3, 15)"),
      nxt_string("defgh") },

    { nxt_string("'abcdefghijklmno'.substr(3, 4)"),
      nxt_string("defg") },

    { nxt_string("'abcdefghijklmno'.substr(-3, 2)"),
      nxt_string("mn") },

    { nxt_string("'abcdefgh'.substr(100, 120)"),
      nxt_string("") },

    { nxt_string("('abc' + 'defgh').substr(1, 4)"),
      nxt_string("bcde") },

    { nxt_string("'abcdefghijklmno'.substring(3, 5)"),
      nxt_string("de") },

    { nxt_string("'abcdefgh'.substring(3)"),
      nxt_string("defgh") },

    { nxt_string("'abcdefgh'.substring(5, 3)"),
      nxt_string("de") },

    { nxt_string("'abcdefgh'.substring(100, 120)"),
      nxt_string("") },

    { nxt_string("'abcdefghijklmno'.slice(NaN, 5)"),
      nxt_string("abcde") },

    { nxt_string("'abcdefghijklmno'.slice(NaN, Infinity)"),
      nxt_string("abcdefghijklmno") },

    { nxt_string("'abcdefghijklmno'.slice(-Infinity, Infinity)"),
      nxt_string("abcdefghijklmno") },

    { nxt_string("'abcdefghijklmno'.slice('0', '5')"),
      nxt_string("abcde") },

    { nxt_string("'abcdefghijklmno'.slice(3, 5)"),
      nxt_string("de") },

    { nxt_string("'abcdefgh'.slice(3)"),
      nxt_string("defgh") },

    { nxt_string("'abcdefgh'.slice(3, -2)"),
      nxt_string("def") },

    { nxt_string("'abcdefgh'.slice(5, 3)"),
      nxt_string("") },

    { nxt_string("'abcdefgh'.slice(100, 120)"),
      nxt_string("") },

    { nxt_string("'abc'.charAt(1 + 1)"),
      nxt_string("c") },

    { nxt_string("'abc'.charAt(3)"),
      nxt_string("") },

    { nxt_string("'abc'.charAt(undefined)"),
      nxt_string("a") },

    { nxt_string("'abc'.charAt(null)"),
      nxt_string("a") },

    { nxt_string("'abc'.charAt(false)"),
      nxt_string("a") },

    { nxt_string("'abc'.charAt(true)"),
      nxt_string("b") },

    { nxt_string("'abc'.charAt(NaN)"),
      nxt_string("a") },

    { nxt_string("'abc'.charAt(Infinity)"),
      nxt_string("") },

    { nxt_string("'abc'.charAt(-Infinity)"),
      nxt_string("") },

    { nxt_string("var o = { valueOf: function() {return 2} }"
                 "'abc'.charAt(o)"),
      nxt_string("c") },

    { nxt_string("var o = { toString: function() {return '2'} }"
                 "'abc'.charAt(o)"),
      nxt_string("c") },

    { nxt_string("'abc'.charCodeAt(1 + 1)"),
      nxt_string("99") },

    { nxt_string("'abc'.charCodeAt(3)"),
      nxt_string("NaN") },

    { nxt_string("a = 'abcdef'; a.3"),
      nxt_string("SyntaxError") },

    { nxt_string("'abcdef'[3]"),
      nxt_string("d") },

    { nxt_string("'abcdef'[0]"),
      nxt_string("a") },

    { nxt_string("'abcdef'[-1]"),
      nxt_string("undefined") },

    { nxt_string("'abcdef'[NaN]"),
      nxt_string("undefined") },

    { nxt_string("'abcdef'[3.5]"),
      nxt_string("undefined") },

    { nxt_string("'abcdef'[8]"),
      nxt_string("undefined") },

    { nxt_string("a = 'abcdef'; b = 1 + 2; a[b]"),
      nxt_string("d") },

    /**/

    { nxt_string("'abc'.toString()"),
      nxt_string("abc") },

    { nxt_string("''.toString.call('abc')"),
      nxt_string("abc") },

    { nxt_string("String.prototype.toString.call('abc')"),
      nxt_string("abc") },

    { nxt_string("String.prototype.toString.call(new String('abc'))"),
      nxt_string("abc") },

    { nxt_string("String.prototype.toString.call(1)"),
      nxt_string("TypeError") },

    { nxt_string("'abc'.valueOf()"),
      nxt_string("abc") },

    /**/

    { nxt_string("var n = { toString: function() { return 1 } };   '12'[n]"),
      nxt_string("2") },

    { nxt_string("var n = { toString: function() { return '1' } }; '12'[n]"),
      nxt_string("2") },

    { nxt_string("var n = { toString: function() { return 1 },"
                          " valueOf:  function() { return 0 } };   '12'[n]"),
      nxt_string("2") },

    /* Externals. */

    { nxt_string("typeof $r"),
      nxt_string("undefined") },

    { nxt_string("a = $r.uri; s = a.fromUTF8(); s.length +' '+ s"),
      nxt_string("3 АБВ") },

    { nxt_string("a = $r.uri; s = a.fromUTF8(2); s.length +' '+ s"),
      nxt_string("2 БВ") },

    { nxt_string("a = $r.uri; s = a.fromUTF8(2, 4); s.length +' '+ s"),
      nxt_string("1 Б") },

    { nxt_string("a = $r.uri; a +' '+ a.length +' '+ a"),
      nxt_string("АБВ 6 АБВ") },

    { nxt_string("$r.uri = 'αβγ'; a = $r.uri; a.length +' '+ a"),
      nxt_string("6 αβγ") },

    { nxt_string("$r.uri.length +' '+ $r.uri"),
      nxt_string("6 АБВ") },

    { nxt_string("$r.uri = $r.uri.substr(2); $r.uri.length +' '+ $r.uri"),
      nxt_string("4 БВ") },

    { nxt_string("a = $r.host; a +' '+ a.length +' '+ a"),
      nxt_string("АБВГДЕЁЖЗИЙ 22 АБВГДЕЁЖЗИЙ") },

    { nxt_string("a = $r.host; a.substr(2, 2)"),
      nxt_string("Б") },

    { nxt_string("a = $r.header['User-Agent']; a +' '+ a.length +' '+ a"),
      nxt_string("User-Agent|АБВ 17 User-Agent|АБВ") },

    { nxt_string("var a='';"
                 "for (p in $r.header) { a += p +':'+ $r.header[p] +',' }"
                 "a"),
      nxt_string("01:01|АБВ,02:02|АБВ,03:03|АБВ,") },

    { nxt_string("$r.some_method('YES')"),
      nxt_string("АБВ") },

    { nxt_string("for (p in $r.some_method);"),
      nxt_string("undefined") },

    { nxt_string("'uri' in $r"),
      nxt_string("true") },

    { nxt_string("'one' in $r"),
      nxt_string("false") },

    { nxt_string("delete $r.uri"),
      nxt_string("false") },

    { nxt_string("delete $r.one"),
      nxt_string("false") },

#if 0
    { nxt_string("$r.some_method.call($r, 'YES')"),
      nxt_string("АБВ") },

    { nxt_string("$r.some_method.apply($r, ['YES'])"),
      nxt_string("АБВ") },
#endif

    { nxt_string("$r.nonexistent"),
      nxt_string("undefined") },

    { nxt_string("$r.error = 'OK'"),
      nxt_string("OK") },

    { nxt_string("var a = { toString: function() { return 1 } }; a"),
      nxt_string("1") },

    { nxt_string("var a = { valueOf: function() { return 1 } };  a"),
      nxt_string("[object Object]") },

    { nxt_string("var a = { toString: 2,"
                 "          valueOf: function() { return 1 } };  a"),
      nxt_string("1") },

    { nxt_string("var a = { toString: function() { return [] },"
                 "          valueOf: function() { return 1 } };  a"),
      nxt_string("1") },

    /**/

    { nxt_string("'абвгдеёжзийклмнопрстуфхцчшщъыьэюя'.charCodeAt(5)"),
      nxt_string("1077") },

    { nxt_string("'12345абвгдеёжзийклмнопрстуфхцчшщъыьэюя'.charCodeAt(35)"),
      nxt_string("1101") },

    { nxt_string("'12345абвгдеёжзийклмнопрстуфхцчшщъыьэюя'.substring(35)"),
      nxt_string("эюя") },

    { nxt_string("'abcdef'.substr(-5, 4).substring(3, 1).charAt(1)"),
      nxt_string("d") },

    { nxt_string("'abcdef'.substr(2, 4).charAt(2)"),
      nxt_string("e") },

    { nxt_string("a = 'abcdef'.substr(2, 4).charAt(2).length"),
      nxt_string("1") },

    { nxt_string("a = 'abcdef'.substr(2, 4).charAt(2) + '1234'"),
      nxt_string("e1234") },

    { nxt_string("a = ('abcdef'.substr(2, 5 * 2 - 6).charAt(2) + '1234')"
                 "    .length"),
      nxt_string("5") },

    { nxt_string("String.fromCharCode('_')"),
      nxt_string("RangeError") },

    { nxt_string("String.fromCharCode(3.14)"),
      nxt_string("RangeError") },

    { nxt_string("String.fromCharCode(65, 90)"),
      nxt_string("AZ") },

    { nxt_string("String.fromCharCode(945, 946, 947)"),
      nxt_string("αβγ") },

    { nxt_string("(function() {"
                 "    for (n = 0; n <= 1114111; n++) {"
                 "        if (String.fromCharCode(n).charCodeAt(0) !== n)"
                 "            return n;"
                 "    }"
                 "    return -1"
                 "})()"),
      nxt_string("-1") },

    { nxt_string("a = 'abcdef'; function f(a) {"
                 "return a.slice(a.indexOf('cd')) } f(a)"),
      nxt_string("cdef") },

    { nxt_string("a = 'abcdef'; a.slice(a.indexOf('cd'))"),
      nxt_string("cdef") },

    { nxt_string("'abcdef'.indexOf('de', 2)"),
      nxt_string("3") },

    { nxt_string("'абвгдеёжзийклмнопрстуфхцчшщъыьэюя'.indexOf('лмно', 10)"),
      nxt_string("12") },

    { nxt_string("'abcdef'.indexOf('a', 10)"),
      nxt_string("-1") },

    { nxt_string("'abcdef'.indexOf('q', 0)"),
      nxt_string("-1") },

    { nxt_string("'abcdef'.indexOf('', 10)"),
      nxt_string("6") },

    { nxt_string("'abcdef'.indexOf('', 3)"),
      nxt_string("3") },

    { nxt_string("'12345'.indexOf()"),
      nxt_string("-1") },

    { nxt_string("'12345'.indexOf(45, '0')"),
      nxt_string("3") },

    { nxt_string("''.indexOf.call(12345, 45, '0')"),
      nxt_string("3") },

    { nxt_string("'abc abc abc abc'.lastIndexOf('abc')"),
      nxt_string("12") },

    { nxt_string("'abc abc abc abc'.lastIndexOf('abc', 11)"),
      nxt_string("8") },

    { nxt_string("'abc abc abc abc'.lastIndexOf('abc', 0)"),
      nxt_string("-1") },

    { nxt_string("'abcdefgh'.search()"),
      nxt_string("0") },

    { nxt_string("'abcdefgh'.search('')"),
      nxt_string("0") },

    { nxt_string("'abcdefgh'.search(undefined)"),
      nxt_string("0") },

    { nxt_string("'abcdefgh'.search(/def/)"),
      nxt_string("3") },

    { nxt_string("'abcdefgh'.search('def')"),
      nxt_string("3") },

    { nxt_string("'123456'.search('45')"),
      nxt_string("3") },

    { nxt_string("'123456'.search(45)"),
      nxt_string("3") },

    { nxt_string("'123456'.search(String(45))"),
      nxt_string("3") },

    { nxt_string("'123456'.search(Number('45'))"),
      nxt_string("3") },

    { nxt_string("var r = { toString: function() { return '45' } }"
                 "'123456'.search(r)"),
      nxt_string("3") },

    { nxt_string("var r = { toString: function() { return 45 } }"
                 "'123456'.search(r)"),
      nxt_string("3") },

    { nxt_string("var r = { toString: function() { return /45/ } }"
                 "'123456'.search(r)"),
      nxt_string("TypeError") },

    { nxt_string("var r = { toString: function() { return /34/ },"
                 "          valueOf:  function() { return 45 } }"
                 "'123456'.search(r)"),
      nxt_string("3") },

    { nxt_string("'abcdefgh'.match()"),
      nxt_string("") },

    { nxt_string("'abcdefgh'.match('')"),
      nxt_string("") },

    { nxt_string("'abcdefgh'.match(undefined)"),
      nxt_string("") },

    { nxt_string("'abcdefgh'.match(/def/)"),
      nxt_string("def") },

    { nxt_string("'abcdefgh'.match('def')"),
      nxt_string("def") },

    { nxt_string("'123456'.match('45')"),
      nxt_string("45") },

    { nxt_string("'123456'.match(45)"),
      nxt_string("45") },

    { nxt_string("'123456'.match(String(45))"),
      nxt_string("45") },

    { nxt_string("'123456'.match(Number('45'))"),
      nxt_string("45") },

    { nxt_string("var r = { toString: function() { return '45' } }"
                 "'123456'.match(r)"),
      nxt_string("45") },

    { nxt_string("var r = { toString: function() { return 45 } }"
                 "'123456'.match(r)"),
      nxt_string("45") },

    { nxt_string("var r = { toString: function() { return /45/ } }"
                 "'123456'.match(r)"),
      nxt_string("TypeError") },

    { nxt_string("var r = { toString: function() { return /34/ },"
                 "          valueOf:  function() { return 45 } }"
                 "'123456'.match(r)"),
      nxt_string("45") },

    { nxt_string("''.match(/^$/)"),
      nxt_string("") },

    { nxt_string("''.match(/^$/g)"),
      nxt_string("") },

    { nxt_string("'abcdefgh'.match(/def/)"),
      nxt_string("def") },

    { nxt_string("'abc ABC aBc'.match(/abc/ig)"),
      nxt_string("abc,ABC,aBc") },

    { nxt_string("var a = 'α'.match(/α/g)[0] + 'α';"
                 "a +' '+ a.length"),
      nxt_string("αα 2") },

    { nxt_string("var a = '\\u00CE\\u00B1'.toBytes().match(/α/g)[0] + 'α';"
                 "a +' '+ a.length"),
      nxt_string("αα 4") },

    /* Functions. */

    { nxt_string("function f() { } f()"),
      nxt_string("undefined") },

    { nxt_string("function f() { ; } f()"),
      nxt_string("undefined") },

    { nxt_string("function f() { ;; } f()"),
      nxt_string("undefined") },

    { nxt_string("function f() { return } f()"),
      nxt_string("undefined") },

    { nxt_string("function f() { return; } f()"),
      nxt_string("undefined") },

    { nxt_string("function f() { return;; } f()"),
      nxt_string("undefined") },

    { nxt_string("function f() { return 1 } f()"),
      nxt_string("1") },

    { nxt_string("function f() { return 1; } f()"),
      nxt_string("1") },

    { nxt_string("function f() { return 1;; } f()"),
      nxt_string("1") },

    { nxt_string("function f() { return 1\n 2 } f()"),
      nxt_string("1") },

    { nxt_string("function f(a) { if (a) return 'OK' } f(1)+f(0)"),
      nxt_string("OKundefined") },

    { nxt_string("function f(a) { if (a) return 'OK'; } f(1)+f(0)"),
      nxt_string("OKundefined") },

    { nxt_string("function f(a) { if (a) return 'OK';; } f(1)+f(0)"),
      nxt_string("OKundefined") },

    { nxt_string("var a = 1; a()"),
      nxt_string("TypeError") },

    { nxt_string("var o = {a:1}; o.a()"),
      nxt_string("TypeError") },

    { nxt_string("(function(){})()"),
      nxt_string("undefined") },

    { nxt_string("var q = 1; function x(a, b, c) { q = a } x(5); q"),
      nxt_string("5") },

    { nxt_string("function x(a) { while (a < 2) a++; return a + 1 } x(1) "),
      nxt_string("3") },

    /* Recursive factorial. */

    { nxt_string("function f(a) {"
                 "    if (a > 1)"
                 "        return a * f(a - 1)"
                 "    return 1"
                 "}"
                 "f(10)"),
      nxt_string("3628800") },

    /* Recursive factorial. */

    { nxt_string("function f(a) { return (a > 1) ? a * f(a - 1) : 1 } f(10)"),
      nxt_string("3628800") },

    { nxt_string("var g = function f(a) { return (a > 1) ? a * f(a - 1) : 1 };"
                 "g(10)"),
      nxt_string("3628800") },

    { nxt_string("(function f(a) { return (a > 1) ? a * f(a - 1) : 1 })(10)"),
      nxt_string("3628800") },

    /* Recursive fibonacci. */

    { nxt_string("function fibo(n) {"
                 "    if (n > 1)"
                 "        return fibo(n-1) + fibo(n-2)"
                 "     return 1"
                 "}"
                 "fibo(10)"),
      nxt_string("89") },

    { nxt_string("function fibo(n) {"
                 "    if (n > 1)"
                 "        return fibo(n-1) + fibo(n-2)"
                 "     return '.'"
                 "}"
                 "fibo(10).length"),
      nxt_string("89") },

    { nxt_string("function fibo(n) {"
                 "    if (n > 1)"
                 "        return fibo(n-1) + fibo(n-2)"
                 "     return 1"
                 "}"
                 "fibo('10')"),
      nxt_string("89") },

    { nxt_string("function add(a, b) { return a + b }"
                 "function mul(a, b) { return a * b }"
                 "function f(a, b) {"
                 "    return a + mul(add(1, 2), add(2, 3)) + b"
                 "}"
                 "f(30, 70)"),
      nxt_string("115") },

    { nxt_string("function a(x, y) { return x + y }"
                 "function b(x, y) { return x * y }"
                 "a(3, b(4, 5))"),
      nxt_string("23") },

    { nxt_string("function x(n) { return n }; x('12'.substr(1))"),
      nxt_string("2") },

    { nxt_string("function f(a) { a *= 2 } f(10)"),
      nxt_string("undefined") },

    { nxt_string("function f() { return 5 } f()"),
      nxt_string("5") },

    { nxt_string("function g(x) { return x + 1 }"
                 "function f(x) { return x } f(g)(2)"),
      nxt_string("3") },

    { nxt_string("function f() { return 5 } f(1)"),
      nxt_string("5") },

    { nxt_string("function f() {} f()"),
      nxt_string("undefined") },

    { nxt_string("function f() {;} f()"),
      nxt_string("undefined") },

    { nxt_string("function f(){return} f()"),
      nxt_string("undefined") },

    { nxt_string("function f(){return;} f()"),
      nxt_string("undefined") },

    { nxt_string("function f(){return\n1} f()"),
      nxt_string("undefined") },

    { nxt_string("function f(a) { return a + 1 } b = f(2)"),
      nxt_string("3") },

    { nxt_string("f = function(a) { a *= 2; return a }; f(10)"),
      nxt_string("20") },

    { nxt_string("var f = function b(a) { a *= 2; return a }; f(10)"),
      nxt_string("20") },

    { nxt_string("var f = function b(a) { a *= 2; return a }; b(10)"),
      nxt_string("ReferenceError") },

    { nxt_string("var f = function(a) { a *= 2; return a }; f(10)"),
      nxt_string("20") },

    { nxt_string("f = function b(a) { a *= 2; return a }; b(10)"),
      nxt_string("ReferenceError") },

    { nxt_string("f = function b(a) { a *= 2; return a }; f(10)"),
      nxt_string("20") },

    { nxt_string("f = a = function(a) { a *= 2; return a }; f(10)"),
      nxt_string("20") },

    { nxt_string("f = a = function(a) { a *= 2; return a }; a(10)"),
      nxt_string("20") },

    { nxt_string("var f = function b(a) { a *= 2; return a } = 5"),
      nxt_string("SyntaxError") },

    { nxt_string("function a() { return { x:2} }; var b = a(); b.x"),
      nxt_string("2") },

    { nxt_string("a = {}; function f(a) { return a + 1 } a.b = f(2); a.b"),
      nxt_string("3") },

    { nxt_string("(function(x) { return x + 1 })(2)"),
      nxt_string("3") },

    { nxt_string("(function(x) { return x + 1 }(2))"),
      nxt_string("3") },

    { nxt_string("a = (function() { return 1 })(); a"),
      nxt_string("1") },

    { nxt_string("a = (function(a) { return a + 1 })(2); a"),
      nxt_string("3") },

    { nxt_string("a = (function(a) { return a + 1 }(2)); a"),
      nxt_string("3") },

    { nxt_string("a = +function(a) { return a + 1 }(2); a"),
      nxt_string("3") },

    { nxt_string("a = -function(a) { return a + 1 }(2); a"),
      nxt_string("-3") },

    { nxt_string("a = !function(a) { return a + 1 }(2); a"),
      nxt_string("false") },

    { nxt_string("a = ~function(a) { return a + 1 }(2); a"),
      nxt_string("-4") },

    { nxt_string("a = void function(a) { return a + 1 }(2); a"),
      nxt_string("undefined") },

    { nxt_string("a = true && function(a) { return a + 1 }(2); a"),
      nxt_string("3") },

    { nxt_string("a = 0, function(a) { return a + 1 }(2); a"),
      nxt_string("0") },

    { nxt_string("a = (0, function(a) { return a + 1 }(2)); a"),
      nxt_string("3") },

    { nxt_string("var a = 0, function(a) { return a + 1 }(2); a"),
      nxt_string("SyntaxError") },

    { nxt_string("var a = (0, function(a) { return a + 1 }(2)); a"),
      nxt_string("3") },

    { nxt_string("var a = +function f(a) { return a + 1 }(2)"
                 "var b = f(5); a"),
      nxt_string("ReferenceError") },

    { nxt_string("var o = { f: function(a) { return a * 2 } }; o.f(5)"),
      nxt_string("10") },

    { nxt_string("var o = {}; o.f = function(a) { return a * 2 }; o.f(5)"),
      nxt_string("10") },

    { nxt_string("var o = { x: 1, f: function() { return this.x } } o.f()"),
      nxt_string("1") },

    { nxt_string("var o = { x: 1, f: function(a) { return this.x += a } }"
                 "o.f(5) +' '+ o.x"),
      nxt_string("6 6") },

    { nxt_string("var f = function(a) { return 3 } f.call()"),
      nxt_string("3") },

    { nxt_string("var f = function(a) { return this } f.call(5)"),
      nxt_string("5") },

    { nxt_string("var f = function(a, b) { return this + a } f.call(5, 1)"),
      nxt_string("6") },

    { nxt_string("var f = function(a, b) { return this + a + b }"
                 "f.call(5, 1, 2)"),
      nxt_string("8") },

    { nxt_string("var f = function(a) { return 3 } f.apply()"),
      nxt_string("3") },

    { nxt_string("var f = function(a) { return this } f.apply(5)"),
      nxt_string("5") },

    { nxt_string("var f = function(a) { return this + a } f.apply(5, 1)"),
      nxt_string("TypeError") },

    { nxt_string("var f = function(a, b) { return this + a + b }"
                 "f.apply(5, [1, 2])"),
      nxt_string("8") },

    { nxt_string("var f = function(a, b) { return this + a + b }"
                 "f.apply(5, [1, 2], 3)"),
      nxt_string("8") },

    { nxt_string("var a = function() { return 1 } + ''; a"),
      nxt_string("[object Function]") },

    { nxt_string("''.concat.call()"),
      nxt_string("TypeError") },

    { nxt_string("''.concat.call('a', 'b', 'c')"),
      nxt_string("abc") },

    { nxt_string("''.concat.call('a')"),
      nxt_string("a") },

    { nxt_string("''.concat.call('a', [ 'b', 'c' ])"),
      nxt_string("ab,c") },

    { nxt_string("''.concat.call('a', [ 'b', 'c' ], 'd')"),
      nxt_string("ab,cd") },

    { nxt_string("''.concat.apply()"),
      nxt_string("TypeError") },

    { nxt_string("''.concat.apply('a')"),
      nxt_string("a") },

    { nxt_string("''.concat.apply('a', 'b')"),
      nxt_string("TypeError") },

    { nxt_string("''.concat.apply('a', [ 'b', 'c' ])"),
      nxt_string("abc") },

    { nxt_string("''.concat.apply('a', [ 'b', 'c' ], 'd')"),
      nxt_string("abc") },

    { nxt_string("[].join.call([1,2,3])"),
      nxt_string("1,2,3") },

    { nxt_string("[].join.call([1,2,3], ':')"),
      nxt_string("1:2:3") },

    { nxt_string("[].join.call([1,2,3], 55)"),
      nxt_string("1552553") },

    { nxt_string("[].join.call()"),
      nxt_string("TypeError") },

    { nxt_string("[].slice.call()"),
      nxt_string("TypeError") },

    { nxt_string("function f(a) {} ; var a = f; var b = f; a === b"),
      nxt_string("true") },

    { nxt_string("function f() {} ; f.toString()"),
      nxt_string("[object Function]") },

    { nxt_string("function f() {}; f"),
      nxt_string("[object Function]") },

    { nxt_string("function f() {}; f = f + 1; f"),
      nxt_string("[object Function]1") },

#if 0
    { nxt_string("function f() {}; f += 1; f"),
      nxt_string("[object Function]1") },
#endif

#if 0
    { nxt_string("function f() {}; function g() { return f }; g()"),
      nxt_string("[object Function]") },
#endif

    { nxt_string("function f(a) { return this+a }; var a = f; a.call('0', 1)"),
      nxt_string("01") },

    { nxt_string("function f(a) { return this+a }; f.call('0', 1)"),
      nxt_string("01") },

    { nxt_string("function f(a) { return this+a };"
                 "function g(f, a, b) { return f.call(a, b) }; g(f, '0', 1)"),
      nxt_string("01") },

    { nxt_string("function f(a) { return this+a };"
                 "o = { g: function (f, a, b) { return f.call(a, b) } };"
                 "o.g(f, '0', 1)"),
      nxt_string("01") },

    { nxt_string("var concat = ''.concat; concat(1,2,3)"),
      nxt_string("TypeError") },

    { nxt_string("var concat = ''.concat; concat.call(1,2,3)"),
      nxt_string("123") },

    { nxt_string("var concat = ''.concat; concat.yes = 'OK';"
                 "concat.call(1,2,3, concat.yes)"),
      nxt_string("123OK") },

    { nxt_string("var f = function(a, b) { return this + a + b }"
                 "var b = f.bind('1'); b('2', '3')"),
      nxt_string("123") },

    { nxt_string("var f = function(a, b) { return this + a + b }"
                 "var b = f.bind('1', '2'); b('3')"),
      nxt_string("123") },

    { nxt_string("var f = function(a, b) { return this + a + b }"
                 "var b = f.bind('1', 2, '3'); b()"),
      nxt_string("123") },

    { nxt_string("var f = function(a, b) { return this + a + b }"
                 "var b = f.bind('1'); b.call('0', '2', '3')"),
      nxt_string("123") },

    { nxt_string("var f = function(a, b) { return this + a + b }"
                 "var b = f.bind('1', '2'); b.call('0', '3')"),
      nxt_string("123") },

    { nxt_string("var f = function(a, b) { return this + a + b }"
                 "var b = f.bind('1', '2', '3'); b.call('0')"),
      nxt_string("123") },

    { nxt_string("var f = function(a, b) { return this + a + b }"
                 "var b = f.bind('1', '2', '3'); b.call()"),
      nxt_string("123") },

    { nxt_string("var f = function(a, b) { return this + a + b }"
                 "var b = f.bind('1'); b.apply('0', ['2', '3'])"),
      nxt_string("123") },

    { nxt_string("var f = function(a, b) { return this + a + b }"
                 "var b = f.bind('1', '2'); b.apply('0', ['3'])"),
      nxt_string("123") },

    { nxt_string("var f = function(a, b) { return this + a + b }"
                 "var b = f.bind('1', '2', '3'); b.apply('0')"),
      nxt_string("123") },

    { nxt_string("var f = function(a, b) { return this + a + b }"
                 "var b = f.bind('1', '2', '3'); b.apply()"),
      nxt_string("123") },

    { nxt_string("function F(a, b) { this.a = a + b }"
                 "var o = new F(1, 2)"
                 "o.a"),
      nxt_string("3") },

    { nxt_string("function F(a, b) { this.a = a + b; return { a: 7 } }"
                 "var o = new F(1, 2)"
                 "o.a"),
      nxt_string("7") },

    { nxt_string("function F(a, b) { return }"
                 "F.prototype.constructor === F"),
      nxt_string("true") },

    { nxt_string("function F() { return }"
                 "F.prototype.ok = 'OK';"
                 "var o = new F(); o.ok"),
      nxt_string("OK") },

    { nxt_string("function F() { return }"
                 "var o = new F();"
                 "o.constructor === F"),
      nxt_string("true") },

    { nxt_string("function a() { return function(x) { return x + 1 } }"
                 "b = a(); b(2)"),
      nxt_string("3") },

    /* RegExp. */

    { nxt_string("/^$/.test('')"),
      nxt_string("true") },

    { nxt_string("var a = /\\d/; a.test('123')"),
      nxt_string("true") },

    { nxt_string("var a = /\\d/; a.test('abc')"),
      nxt_string("false") },

    { nxt_string("/\\d/.test('123')"),
      nxt_string("true") },

    { nxt_string("/\\d/.test(123)"),
      nxt_string("true") },

    { nxt_string("/undef/.test()"),
      nxt_string("true") },

    { nxt_string("var s = { toString: function() { return 123 } };"
                 "/\\d/.test(s)"),
      nxt_string("true") },

    { nxt_string("/\\d/.test('abc')"),
      nxt_string("false") },

    { nxt_string("/abc/i.test('ABC')"),
      nxt_string("true") },

    { nxt_string("/абв/i.test('АБВ')"),
      nxt_string("true") },

    { nxt_string("/\\x80/.test('\\u0080')"),
      nxt_string("true") },

    { nxt_string("/\\x80/.test('\\u0080'.toBytes())"),
      nxt_string("true") },

    { nxt_string("/α/.test('\\u03B1')"),
      nxt_string("true") },

    { nxt_string("/α/.test('\\u00CE\\u00B1'.toBytes())"),
      nxt_string("true") },

    { nxt_string("/\\d/.exec('123')"),
      nxt_string("1") },

    { nxt_string("/\\d/.exec(123)"),
      nxt_string("1") },

    { nxt_string("/undef/.exec()"),
      nxt_string("undef") },

    { nxt_string("var s = { toString: function() { return 123 } };"
                 "/\\d/.exec(s)"),
      nxt_string("1") },

    { nxt_string("var a = /^$/.exec(''); a.length +' '+ a"),
      nxt_string("1 ") },

    { nxt_string("var r = /бв/ig;"
                 "var a = r.exec('АБВ');"
                 "r.lastIndex +' '+ a +' '+ "
                 "r.source +' '+ r.source.length +' '+ r"),
      nxt_string("3 БВ бв 2 /бв/gi") },

    { nxt_string("var r = /\\x80/g; r.exec('\\u0081\\u0080'.toBytes());"
                 "r.lastIndex +' '+ r.source +' '+ r.source.length +' '+ r"),
      nxt_string("1 \\x80 4 /\\x80/g") },

    /*
     * It seems that "/стоп/ig" fails on early PCRE versions.
     * It fails at least in 8.1 and works at least in 8.31.
     */

    { nxt_string("var r = /Стоп/ig;"
                 "var a = r.exec('АБВДЕЁЖЗИКЛМНОПРСТУФХЦЧШЩЬЫЪЭЮЯСТОП');"
                 "r.lastIndex +' '+ a +' '+ r.source +' '+ r"),
      nxt_string("35 СТОП Стоп /Стоп/gi") },

    { nxt_string("var r = /quick\\s(brown).+?(jumps)/ig;"
                 "var a = r.exec('The Quick Brown Fox Jumps Over The Lazy Dog')"
                 "a[0] +' '+ a[1] +' '+ a[2] +' '+ a[3] +' '+ "
                 "a.index +' '+ r.lastIndex +' '+ a.input"),
      nxt_string("Quick Brown Fox Jumps Brown Jumps undefined "
                 "4 25 The Quick Brown Fox Jumps Over The Lazy Dog") },

    { nxt_string("var s; var r = /./g; while (s = r.exec('abc')); s"),
      nxt_string("null") },

    { nxt_string("var r = /LS/i.exec(false); r[0]"),
      nxt_string("ls") },

    { nxt_string("var r = /./; r"),
      nxt_string("/./") },

    { nxt_string("var r = new RegExp(); r"),
      nxt_string("/(?:)/") },

    { nxt_string("var r = new RegExp('.'); r"),
      nxt_string("/./") },

    { nxt_string("var r = new RegExp('.', 'ig'); r"),
      nxt_string("/./gi") },

    { nxt_string("var r = new RegExp('abc'); r.test('00abc11')"),
      nxt_string("true") },

    { nxt_string("var r = new RegExp('abc', 'i'); r.test('00ABC11')"),
      nxt_string("true") },

    /* Non-standard ECMA-262 features. */

    /* 0x10400 is not a surrogate pair of 0xD801 and 0xDC00. */

    { nxt_string("var chars = '𐐀'; chars.length +' '+ chars.charCodeAt(0)"),
      nxt_string("1 66560") },

    /* es5id: 6.1, 0x104A0 is not a surrogate pair of 0xD801 and 0xDCA0. */

    { nxt_string("var chars = '𐒠'; chars.length +' '+ chars.charCodeAt(0)"),
      nxt_string("1 66720") },

    /* Exceptions. */

    { nxt_string("throw null"),
      nxt_string("") },

    { nxt_string("var a; try { throw null } catch (e) { a = e } a"),
      nxt_string("null") },

    { nxt_string("try { throw null } catch (e) { throw e }"),
      nxt_string("") },

    { nxt_string("var a = 0; try { a = 5 }"
                 "catch (e) { a = 9 } finally { a++ } a"),
      nxt_string("6") },

    { nxt_string("var a = 0; try { throw 3 }"
                 "catch (e) { a = e } finally { a++ } a"),
      nxt_string("4") },

    { nxt_string("var a = 0; try { throw 3 }"
                 "catch (e) { throw e + 1 } finally { a++ }"),
      nxt_string("") },

    { nxt_string("var a = 0; try { throw 3 }"
                 "catch (e) { a = e } finally { throw a }"),
      nxt_string("") },

    { nxt_string("try { throw null } catch (e) { } finally { }"),
      nxt_string("undefined") },

    { nxt_string("var a = 0; try { throw 3 }"
                 "catch (e) { throw 4 } finally { throw a }"),
      nxt_string("") },

    { nxt_string("var a = 0; try { a = 5 } finally { a++ } a"),
      nxt_string("6") },

    { nxt_string("var a = 0; try { throw 5 } finally { a++ }"),
      nxt_string("") },

    { nxt_string("var a = 0; try { a = 5 } finally { throw 7 }"),
      nxt_string("") },

    { nxt_string("function f(a) {"
                 "   if (a > 1) return f(a - 1);"
                 "   throw 9; return a }"
                 "var a = 0; try { a = f(5); a++ } catch(e) { a = e } a"),
      nxt_string("9") },

    { nxt_string("var a; try { try { throw 5 } catch (e) { a = e } throw 3 }"
                 "       catch(x) { a += x } a"),
      nxt_string("8") },

    { nxt_string("var o = { valueOf: function() { return '3' } }; --o"),
      nxt_string("2") },

    { nxt_string("var o = { valueOf: function() { return [3] } }; --o"),
      nxt_string("NaN") },

    { nxt_string("var o = { valueOf: function() { return '3' } } 10 - o"),
      nxt_string("7") },

    { nxt_string("var o = { valueOf: function() { return [3] } } 10 - o"),
      nxt_string("NaN") },

    { nxt_string("var o = { toString: function() { return 'OK' } } 'o:' + o"),
      nxt_string("o:OK") },

    { nxt_string("var o = { toString: function() { return [1] } } 'o:' + o"),
      nxt_string("TypeError") },

    { nxt_string("var a = { valueOf: function() { return '3' } }"
                 "var b = { toString: function() { return 10 - a + 'OK' } }"
                 "var c = { toString: function() { return b + 'YES' } }"
                 "'c:' + c"),
      nxt_string("c:7OKYES") },

    { nxt_string("[1,2,3].valueOf()"),
      nxt_string("1,2,3") },

    { nxt_string("var o = { valueOf: function() { return 'OK' } } o.valueOf()"),
      nxt_string("OK") },

    { nxt_string("false.__proto__ === true.__proto__"),
      nxt_string("true") },

    { nxt_string("0..__proto__ === 1..__proto__"),
      nxt_string("true") },

    { nxt_string("[].__proto__ === [1,2].__proto__"),
      nxt_string("true") },

    { nxt_string("/./.__proto__ === /a/.__proto__"),
      nxt_string("true") },

    { nxt_string("''.__proto__ === 'abc'.__proto__"),
      nxt_string("true") },

    { nxt_string("[].__proto__.join.call([1,2,3], ':')"),
      nxt_string("1:2:3") },

    { nxt_string("''.__proto__.concat.call('a', 'b', 'c')"),
      nxt_string("abc") },

    { nxt_string("/./.__proto__.test.call(/a{2}/, 'aaa')"),
      nxt_string("true") },

    { nxt_string("true instanceof Boolean"),
      nxt_string("false") },

    { nxt_string("1 instanceof Number"),
      nxt_string("false") },

    { nxt_string("'' instanceof String"),
      nxt_string("false") },

    { nxt_string("({}) instanceof Object"),
      nxt_string("true") },

    { nxt_string("[] instanceof []"),
      nxt_string("TypeError") },

    { nxt_string("[] instanceof Array"),
      nxt_string("true") },

    { nxt_string("[] instanceof Object"),
      nxt_string("true") },

    { nxt_string("/./ instanceof RegExp"),
      nxt_string("true") },

    { nxt_string("/./ instanceof Object"),
      nxt_string("true") },

    { nxt_string("var o = Object(); o"),
      nxt_string("[object Object]") },

    { nxt_string("var o = new Object(); o"),
      nxt_string("[object Object]") },

    { nxt_string("var o = new Object(1); o"),
      nxt_string("1") },

    { nxt_string("var o = {}; o === Object(o)"),
      nxt_string("true") },

    { nxt_string("var o = {}; o === new Object(o)"),
      nxt_string("true") },

    { nxt_string("Object.name"),
      nxt_string("Object") },

    { nxt_string("Object.length"),
      nxt_string("1") },

    { nxt_string("Object.__proto__ === Function.prototype"),
      nxt_string("true") },

    { nxt_string("Object.prototype.constructor === Object"),
      nxt_string("true") },

    { nxt_string("Object.prototype.__proto__ === null"),
      nxt_string("true") },

    { nxt_string("Object.constructor === Function"),
      nxt_string("true") },

    { nxt_string("({}).__proto__ === Object.prototype"),
      nxt_string("true") },

    { nxt_string("({}).__proto__.constructor === Object"),
      nxt_string("true") },

    { nxt_string("({}).constructor === Object"),
      nxt_string("true") },

    { nxt_string("var a = Array(3); a"),
      nxt_string(",,") },

    { nxt_string("var a = Array(); a.length"),
      nxt_string("0") },

    { nxt_string("var a = Array(0); a.length"),
      nxt_string("0") },

    { nxt_string("var a = Array(true); a"),
      nxt_string("true") },

    { nxt_string("var a = Array(1,'two',3); a"),
      nxt_string("1,two,3") },

    { nxt_string("var a = Array(-1)"),
      nxt_string("RangeError") },

    { nxt_string("var a = Array(2.5)"),
      nxt_string("RangeError") },

    { nxt_string("var a = Array(NaN)"),
      nxt_string("RangeError") },

    { nxt_string("var a = Array(Infinity)"),
      nxt_string("RangeError") },

    { nxt_string("var a = new Array(3); a"),
      nxt_string(",,") },

    { nxt_string("Array.name"),
      nxt_string("Array") },

    { nxt_string("Array.length"),
      nxt_string("1") },

    { nxt_string("Array.__proto__ === Function.prototype"),
      nxt_string("true") },

    { nxt_string("Array.prototype.constructor === Array"),
      nxt_string("true") },

    { nxt_string("Array.prototype.__proto__ === Object.prototype"),
      nxt_string("true") },

    { nxt_string("Array.constructor === Function"),
      nxt_string("true") },

    { nxt_string("var a = []; a.join = 'OK'; a"),
      nxt_string("[object Array]") },

    { nxt_string("[].__proto__ === Array.prototype"),
      nxt_string("true") },

    { nxt_string("[].__proto__.constructor === Array"),
      nxt_string("true") },

    { nxt_string("[].constructor === Array"),
      nxt_string("true") },

    { nxt_string("Boolean()"),
      nxt_string("false") },

    { nxt_string("Boolean(0)"),
      nxt_string("false") },

    { nxt_string("Boolean('')"),
      nxt_string("false") },

    { nxt_string("Boolean(1)"),
      nxt_string("true") },

    { nxt_string("Boolean('a')"),
      nxt_string("true") },

    { nxt_string("Boolean({})"),
      nxt_string("true") },

    { nxt_string("Boolean([])"),
      nxt_string("true") },

    { nxt_string("typeof Boolean(1)"),
      nxt_string("boolean") },

    { nxt_string("typeof new Boolean(1)"),
      nxt_string("object") },

    { nxt_string("Boolean.name"),
      nxt_string("Boolean") },

    { nxt_string("Boolean.length"),
      nxt_string("1") },

    { nxt_string("Boolean.__proto__ === Function.prototype"),
      nxt_string("true") },

    { nxt_string("Boolean.prototype.constructor === Boolean"),
      nxt_string("true") },

    { nxt_string("Boolean.prototype.__proto__ === Object.prototype"),
      nxt_string("true") },

    { nxt_string("Boolean.constructor === Function"),
      nxt_string("true") },

    { nxt_string("true.__proto__ === Boolean.prototype"),
      nxt_string("true") },

    { nxt_string("false.__proto__ === Boolean.prototype"),
      nxt_string("true") },

    { nxt_string("var b = Boolean(1); b.__proto__ === Boolean.prototype"),
      nxt_string("true") },

    { nxt_string("var b = new Boolean(1); b.__proto__ === Boolean.prototype"),
      nxt_string("true") },

    { nxt_string("Number()"),
      nxt_string("0") },

    { nxt_string("Number(123)"),
      nxt_string("123") },

    { nxt_string("Number('123')"),
      nxt_string("123") },

    { nxt_string("var o = { valueOf: function() { return 123 } };"
                 "Number(o)"),
      nxt_string("123") },

    { nxt_string("var o = { valueOf: function() { return 123 } };"
                 "new Number(o)"),
      nxt_string("123") },

    { nxt_string("typeof Number(1)"),
      nxt_string("number") },

    { nxt_string("typeof new Number(1)"),
      nxt_string("object") },

    { nxt_string("Number.name"),
      nxt_string("Number") },

    { nxt_string("Number.length"),
      nxt_string("1") },

    { nxt_string("Number.__proto__ === Function.prototype"),
      nxt_string("true") },

    { nxt_string("Number.prototype.constructor === Number"),
      nxt_string("true") },

    { nxt_string("Number.prototype.__proto__ === Object.prototype"),
      nxt_string("true") },

    { nxt_string("Number.constructor === Function"),
      nxt_string("true") },

    { nxt_string("0..__proto__ === Number.prototype"),
      nxt_string("true") },

    { nxt_string("var n = Number(1); n.__proto__ === Number.prototype"),
      nxt_string("true") },

    { nxt_string("var n = new Number(1); n.__proto__ === Number.prototype"),
      nxt_string("true") },

    { nxt_string("String()"),
      nxt_string("") },

    { nxt_string("String(123)"),
      nxt_string("123") },

    { nxt_string("new String(123)"),
      nxt_string("123") },

    { nxt_string("String([1,2,3])"),
      nxt_string("1,2,3") },

    { nxt_string("new String([1,2,3])"),
      nxt_string("1,2,3") },

    { nxt_string("var o = { toString: function() { return 'OK' } }"
                 "String(o)"),
      nxt_string("OK") },

    { nxt_string("var o = { toString: function() { return 'OK' } }"
                 "new String(o)"),
      nxt_string("OK") },

    { nxt_string("typeof String('abc')"),
      nxt_string("string") },

    { nxt_string("typeof new String('abc')"),
      nxt_string("object") },

    { nxt_string("String.name"),
      nxt_string("String") },

    { nxt_string("String.length"),
      nxt_string("1") },

    { nxt_string("String.__proto__ === Function.prototype"),
      nxt_string("true") },

    { nxt_string("String.prototype.length"),
      nxt_string("0") },

    { nxt_string("String.prototype.constructor === String"),
      nxt_string("true") },

    { nxt_string("String.prototype.__proto__ === Object.prototype"),
      nxt_string("true") },

    { nxt_string("''.__proto__ === String.prototype"),
      nxt_string("true") },

    { nxt_string("String.constructor === Function"),
      nxt_string("true") },

    { nxt_string("'test'.__proto__ === String.prototype"),
      nxt_string("true") },

    { nxt_string("var s = String('abc'); s.__proto__ === String.prototype"),
      nxt_string("true") },

    { nxt_string("var s = new String('abc'); s.__proto__ === String.prototype"),
      nxt_string("true") },

    { nxt_string("'test'.constructor === String"),
      nxt_string("true") },

    { nxt_string("'test'.constructor.prototype === String.prototype"),
      nxt_string("true") },

    { nxt_string("Function.name"),
      nxt_string("Function") },

    { nxt_string("Function.length"),
      nxt_string("1") },

    { nxt_string("Function.__proto__ === Function.prototype"),
      nxt_string("true") },

    { nxt_string("Function.prototype.constructor === Function"),
      nxt_string("true") },

    { nxt_string("Function.prototype.__proto__ === Object.prototype"),
      nxt_string("true") },

    { nxt_string("Function.constructor === Function"),
      nxt_string("true") },

    { nxt_string("function f() {} f.__proto__ === Function.prototype"),
      nxt_string("true") },

    { nxt_string("RegExp()"),
      nxt_string("/(?:)/") },

    { nxt_string("RegExp('')"),
      nxt_string("/(?:)/") },

    { nxt_string("RegExp(123)"),
      nxt_string("/123/") },

    { nxt_string("RegExp.name"),
      nxt_string("RegExp") },

    { nxt_string("RegExp.length"),
      nxt_string("2") },

    { nxt_string("RegExp.__proto__ === Function.prototype"),
      nxt_string("true") },

    { nxt_string("RegExp.prototype.constructor === RegExp"),
      nxt_string("true") },

    { nxt_string("RegExp.prototype.__proto__ === Object.prototype"),
      nxt_string("true") },

    { nxt_string("RegExp.constructor === Function"),
      nxt_string("true") },

    { nxt_string("/./.__proto__ === RegExp.prototype"),
      nxt_string("true") },

    { nxt_string("Object.prototype.toString.call()"),
      nxt_string("[object Undefined]") },

    { nxt_string("Object.prototype.toString.call(undefined)"),
      nxt_string("[object Undefined]") },

    { nxt_string("Object.prototype.toString.call(null)"),
      nxt_string("[object Null]") },

    { nxt_string("Object.prototype.toString.call(true)"),
      nxt_string("[object Boolean]") },

    { nxt_string("Object.prototype.toString.call(1)"),
      nxt_string("[object Number]") },

    { nxt_string("Object.prototype.toString.call('')"),
      nxt_string("[object String]") },

    { nxt_string("Object.prototype.toString.call({})"),
      nxt_string("[object Object]") },

    { nxt_string("Object.prototype.toString.call([])"),
      nxt_string("[object Array]") },

    { nxt_string("Object.prototype.toString.call(new Object(true))"),
      nxt_string("[object Boolean]") },

    { nxt_string("Object.prototype.toString.call(new Number(1))"),
      nxt_string("[object Number]") },

    { nxt_string("Object.prototype.toString.call(new Object(1))"),
      nxt_string("[object Number]") },

    { nxt_string("Object.prototype.toString.call(new Object(''))"),
      nxt_string("[object String]") },

    { nxt_string("Object.prototype.toString.call(function(){})"),
      nxt_string("[object Function]") },

    { nxt_string("Object.prototype.toString.call(/./)"),
      nxt_string("[object RegExp]") },

    { nxt_string("var p = { a:5 }; var o = Object.create(p); o.a"),
      nxt_string("5") },

    { nxt_string("var p = { a:5 }; var o = Object.create(p);"
                 "o.__proto__ === p"),
      nxt_string("true") },

    { nxt_string("var o = Object.create(Object.prototype);"
                 "o.__proto__ === Object.prototype"),
      nxt_string("true") },

    { nxt_string("var o = Object.create(null); '__proto__' in o"),
      nxt_string("false") },

    { nxt_string("var d = new Date(1308895200000); d.getTime()"),
      nxt_string("1308895200000") },

    { nxt_string("var d = new Date(2011, 5, 24, 18, 45); d.getTime()"),
      nxt_string("1308895200000") },

    { nxt_string("var d = new Date(2011, 5, 24, 18, 45); d.valueOf()"),
      nxt_string("1308895200000") },

    { nxt_string("var d = new Date(2011, 5, 24, 18, 45); d"),
      nxt_string("Fri Jun 24 2011 18:45:00 GMT+1245 (CHAST)") },

    { nxt_string("var d = new Date(2011, 5, 24, 18, 45); d.toString()"),
      nxt_string("Fri Jun 24 2011 18:45:00 GMT+1245 (CHAST)") },

    { nxt_string("var d = new Date(2011, 5, 24, 18, 45); d.toDateString()"),
      nxt_string("Fri Jun 24 2011") },

    { nxt_string("var d = new Date(2011, 5, 24, 18, 45); d.toTimeString()"),
      nxt_string("18:45:00 GMT+1245 (CHAST)") },

    { nxt_string("var d = new Date(2011, 5, 24, 18, 45); d.toUTCString()"),
      nxt_string("Fri Jun 24 2011 06:00:00 GMT") },

    { nxt_string("var d = new Date(2011, 5, 24, 18, 45, 12, 625);"
                 "d.toISOString()"),
      nxt_string("2011-06-24T06:00:12.625Z") },

#if 0
    /* These tests fail on Solaris: gmtime_r() returns off by one day. */

    { nxt_string("var d = new Date(-62167219200000); d.toISOString()"),
      nxt_string("0000-01-01T00:00:00.000Z") },

    { nxt_string("var d = new Date(-62135596800000); d.toISOString()"),
      nxt_string("0001-01-01T00:00:00.000Z") },

    { nxt_string("var d = new Date(-62198755200000); d.toISOString()"),
      nxt_string("-000001-01-01T00:00:00.000Z") },
#endif

    { nxt_string("Date.UTC(2011, 5, 24, 6, 0)"),
      nxt_string("1308895200000") },

    { nxt_string("Date.parse()"),
      nxt_string("NaN") },

    { nxt_string("Date.parse('2011-06-24T06:01:02.625Z')"),
      nxt_string("1308895262625") },

    { nxt_string("Date.parse('Fri, 24 Jun 2011 18:48:02 GMT')"),
      nxt_string("1308941282000") },

    { nxt_string("Date.parse('Fri, 24 Jun 2011 18:48:02 +1245')"),
      nxt_string("1308895382000") },

    { nxt_string("Date.parse('Fri Jun 24 2011 18:48:02 GMT+1245')"),
      nxt_string("1308895382000") },

    /* Jan 1, 1. */
    { nxt_string("Date.parse('+000001-01-01T00:00:00.000Z')"),
      nxt_string("-62135596800000") },

    /* Mar 2, 1 BCE. */
    { nxt_string("Date.parse('+000000-03-02T00:00:00.000Z')"),
      nxt_string("-62161948800000") },

    /* Mar 1, 1 BCE. */
    { nxt_string("Date.parse('+000000-03-01T00:00:00.000Z')"),
      nxt_string("-62162035200000") },

    /* Feb 29, 1 BCE. */
    { nxt_string("Date.parse('+000000-02-29T00:00:00.000Z')"),
      nxt_string("-62162121600000") },

    /* Feb 28, 1 BCE. */
    { nxt_string("Date.parse('+000000-02-28T00:00:00.000Z')"),
      nxt_string("-62162208000000") },

    /* Jan 1, 1 BCE. */
    { nxt_string("Date.parse('+000000-01-01T00:00:00.000Z')"),
      nxt_string("-62167219200000") },

    /* Jan 1, 2 BCE. */
    { nxt_string("Date.parse('-000001-01-01T00:00:00.000Z')"),
      nxt_string("-62198755200000") },

    { nxt_string("var d = new Date(); d == Date.parse(d.toISOString())"),
      nxt_string("true") },

    { nxt_string("var s = Date(); s === Date(Date.parse(s))"),
      nxt_string("true") },

    { nxt_string("var n = Date.now(); n == new Date(n)"),
      nxt_string("true") },

    { nxt_string("var d = new Date(2011, 5, 24, 18, 45); d.getFullYear()"),
      nxt_string("2011") },

    { nxt_string("var d = new Date(2011, 5, 24, 18, 45); d.getUTCFullYear()"),
      nxt_string("2011") },

    { nxt_string("var d = new Date(2011, 5, 24, 18, 45); d.getMonth()"),
      nxt_string("5") },

    { nxt_string("var d = new Date(2011, 5, 24, 18, 45); d.getUTCMonth()"),
      nxt_string("5") },

    { nxt_string("var d = new Date(2011, 5, 24, 18, 45); d.getDate()"),
      nxt_string("24") },

    { nxt_string("var d = new Date(2011, 5, 24, 18, 45); d.getUTCDate()"),
      nxt_string("24") },

    { nxt_string("var d = new Date(2011, 5, 24, 18, 45); d.getDay()"),
      nxt_string("5") },

    { nxt_string("var d = new Date(2011, 5, 24, 18, 45); d.getUTCDay()"),
      nxt_string("5") },

    { nxt_string("var d = new Date(2011, 5, 24, 18, 45); d.getHours()"),
      nxt_string("18") },

    { nxt_string("var d = new Date(2011, 5, 24, 18, 45); d.getUTCHours()"),
      nxt_string("6") },

    { nxt_string("var d = new Date(2011, 5, 24, 18, 45); d.getMinutes()"),
      nxt_string("45") },

    { nxt_string("var d = new Date(2011, 5, 24, 18, 45); d.getUTCMinutes()"),
      nxt_string("0") },

    { nxt_string("var d = new Date(2011, 5, 24, 18, 45, 12);"
                 "d.getSeconds()"),
      nxt_string("12") },

    { nxt_string("var d = new Date(2011, 5, 24, 18, 45, 12);"
                 "d.getUTCSeconds()"),
      nxt_string("12") },

    { nxt_string("var d = new Date(2011, 5, 24, 18, 45, 12, 625);"
                 "d.getMilliseconds()"),
      nxt_string("625") },

    { nxt_string("var d = new Date(2011, 5, 24, 18, 45, 12, 625);"
                 "d.getUTCMilliseconds()"),
      nxt_string("625") },

    { nxt_string("var d = new Date(2011, 5, 24, 18, 45, 12, 625);"
                 "d.getTimezoneOffset()"),
      nxt_string("-765") },

    { nxt_string("var d = new Date(); d.setTime(1308895200000); d.getTime()"),
      nxt_string("1308895200000") },

    { nxt_string("var d = new Date(1308895201625); d.setMilliseconds(5003);"
                 "d.getTime()"),
      nxt_string("1308895206003") },

    { nxt_string("var d = new Date(1308895201625); d.setSeconds(2, 5003);"
                 "d.getTime()"),
      nxt_string("1308895207003") },

    { nxt_string("var d = new Date(1308895201625); d.setSeconds(2);"
                 "d.getTime()"),
      nxt_string("1308895202625") },

    { nxt_string("var d = new Date(1308895323625); d.setMinutes(3, 2, 5003);"
                 "d.getTime()"),
      nxt_string("1308892687003") },

    { nxt_string("var d = new Date(1308895323625); d.setMinutes(3, 2);"
                 "d.getTime()"),
      nxt_string("1308892682625") },

    { nxt_string("var d = new Date(1308895323625); d.setMinutes(3);"
                 "d.getTime()"),
      nxt_string("1308892683625") },

    { nxt_string("var d = new Date(1308895323625); d.setUTCMinutes(3, 2, 5003);"
                 "d.getTime()"),
      nxt_string("1308895387003") },

    { nxt_string("var d = new Date(1308895323625); d.setUTCMinutes(3, 2);"
                 "d.getTime()"),
      nxt_string("1308895382625") },

    { nxt_string("var d = new Date(1308895323625); d.setUTCMinutes(3);"
                 "d.getTime()"),
      nxt_string("1308895383625") },

    { nxt_string("var d = new Date(1308895323625); d.setHours(20, 3, 2, 5003);"
                 "d.getTime()"),
      nxt_string("1308899887003") },

    { nxt_string("var d = new Date(1308895323625); d.setHours(20, 3, 2);"
                 "d.getTime()"),
      nxt_string("1308899882625") },

    { nxt_string("var d = new Date(1308895323625); d.setHours(20, 3);"
                 "d.getTime()"),
      nxt_string("1308899883625") },

    { nxt_string("var d = new Date(1308895323625); d.setHours(20);"
                 "d.getTime()"),
      nxt_string("1308902523625") },

    { nxt_string("var d = new Date(1308895323625);"
                 "d.setUTCHours(20, 3, 2, 5003); d.getTime()"),
      nxt_string("1308945787003") },

    { nxt_string("var d = new Date(1308895323625); d.setUTCHours(20, 3, 2);"
                 "d.getTime()"),
      nxt_string("1308945782625") },

    { nxt_string("var d = new Date(1308895323625); d.setUTCHours(20, 3);"
                 "d.getTime()"),
      nxt_string("1308945783625") },

    { nxt_string("var d = new Date(1308895323625); d.setUTCHours(20);"
                 "d.getTime()"),
      nxt_string("1308945723625") },

    { nxt_string("var d = new Date(1308895323625); d.setDate(10);"
                 "d.getTime()"),
      nxt_string("1307685723625") },

    { nxt_string("var d = new Date(1308895323625); d.setUTCDate(10);"
                 "d.getTime()"),
      nxt_string("1307685723625") },

    { nxt_string("var d = new Date(1308895323625); d.setMonth(2, 10);"
                 "d.getTime()"),
      nxt_string("1299733323625") },

    { nxt_string("var d = new Date(1308895323625); d.setUTCMonth(2, 10);"
                 "d.getTime()"),
      nxt_string("1299736923625") },

    { nxt_string("var d = new Date(1308895323625); d.setMonth(2);"
                 "d.getTime()"),
      nxt_string("1300942923625") },

    { nxt_string("var d = new Date(1308895323625); d.setUTCMonth(2);"
                 "d.getTime()"),
      nxt_string("1300946523625") },

    { nxt_string("var d = new Date(1308895323625); d.setFullYear(2010, 2, 10);"
                 "d.getTime()"),
      nxt_string("1268197323625") },

    { nxt_string("var d = new Date(1308895323625);"
                 "d.setUTCFullYear(2010, 2, 10); d.getTime()"),
      nxt_string("1268200923625") },

    { nxt_string("var d = new Date(1308895323625); d.setFullYear(2010, 2);"
                 "d.getTime()"),
      nxt_string("1269406923625") },

    { nxt_string("var d = new Date(1308895323625); d.setUTCFullYear(2010, 2);"
                 "d.getTime()"),
      nxt_string("1269410523625") },

    { nxt_string("var d = new Date(1308895323625); d.setFullYear(2010);"
                 "d.getTime()"),
      nxt_string("1277359323625") },

    { nxt_string("var d = new Date(1308895323625); d.setUTCFullYear(2010);"
                 "d.getTime()"),
      nxt_string("1277359323625") },

    { nxt_string("var d = new Date(2011, 5, 24, 18, 45, 12, 625);"
                 "d.toJSON(1)"),
      nxt_string("2011-06-24T06:00:12.625Z") },

    { nxt_string("var o = { toISOString: function() { return 'OK' } }"
                 "Date.prototype.toJSON.call(o, 1)"),
      nxt_string("OK") },

    { nxt_string("Date.name"),
      nxt_string("Date") },

    { nxt_string("Date.length"),
      nxt_string("7") },

    { nxt_string("Date.__proto__ === Function.prototype"),
      nxt_string("true") },

    { nxt_string("Date.prototype.constructor === Date"),
      nxt_string("true") },

    { nxt_string("Date.prototype.__proto__ === Object.prototype"),
      nxt_string("true") },

    { nxt_string("Date.constructor === Function"),
      nxt_string("true") },

    /* eval(). */

    { nxt_string("eval.name"),
      nxt_string("eval") },

    { nxt_string("eval.length"),
      nxt_string("1") },

    { nxt_string("eval.prototype"),
      nxt_string("undefined") },

    { nxt_string("eval.__proto__ === Function.prototype"),
      nxt_string("true") },

    { nxt_string("eval.constructor === Function"),
      nxt_string("true") },

    { nxt_string("eval()"),
      nxt_string("") },

    /* Math. */

    { nxt_string("Math.PI"),
      nxt_string("3.14159") },

    { nxt_string("Math.abs(5)"),
      nxt_string("5") },

    { nxt_string("Math.abs(-5)"),
      nxt_string("5") },

    { nxt_string("Math.abs('5.0')"),
      nxt_string("5") },

    { nxt_string("Math.abs('abc')"),
      nxt_string("NaN") },

    { nxt_string("Math.max()"),
      nxt_string("-Infinity") },

    { nxt_string("Math.max(null)"),
      nxt_string("0") },

    { nxt_string("Math.max(undefined)"),
      nxt_string("NaN") },

    { nxt_string("Math.max('1', '2', '5')"),
      nxt_string("5") },

    { nxt_string("Math.min()"),
      nxt_string("Infinity") },

    { nxt_string("Math.min(null)"),
      nxt_string("0") },

    { nxt_string("Math.min(undefined)"),
      nxt_string("NaN") },

    { nxt_string("Math.min('1', '2', '5')"),
      nxt_string("1") },

    { nxt_string("Math.pow(2, 5)"),
      nxt_string("32") },

    { nxt_string("Math.pow(2)"),
      nxt_string("NaN") },

    { nxt_string("Math.pow()"),
      nxt_string("NaN") },

    /* ES5FIX: "[object Math]". */

    { nxt_string("Math"),
      nxt_string("[object Object]") },

    /* External interface. */

    { nxt_string("function f(req) { return req.uri }"),
      nxt_string("АБВ") },

    /* Trick: number to boolean. */

    { nxt_string("var a = 0; !!a"),
      nxt_string("false") },

    { nxt_string("var a = 5; !!a"),
      nxt_string("true") },

    /* Trick: flooring. */

    { nxt_string("var n = -10.12345; ~~n"),
      nxt_string("-10") },

    { nxt_string("var n = 10.12345; ~~n"),
      nxt_string("10") },

    /* es5id: 8.2_A1_T1 */
    /* es5id: 8.2_A1_T2 */

    { nxt_string("var x = null;"),
      nxt_string("undefined") },

    /* es5id: 8.2_A2 */

    { nxt_string("var null;"),
      nxt_string("SyntaxError") },

    /* es5id: 8.2_A3 */

    { nxt_string("typeof(null) === \"object\""),
      nxt_string("true") },

};


typedef struct {
    nxt_mem_cache_pool_t  *mem_cache_pool;
    nxt_str_t             uri;
} njs_unit_test_req;


static njs_ret_t
njs_unit_test_r_get_uri_external(njs_vm_t *vm, njs_value_t *value, void *obj,
    uintptr_t data)
{
    njs_unit_test_req  *r;

    r = (njs_unit_test_req *) obj;

    return njs_string_create(vm, value, r->uri.data, r->uri.len, 0);
}


static njs_ret_t
njs_unit_test_r_set_uri_external(njs_vm_t *vm, void *obj, uintptr_t data,
    nxt_str_t *value)
{
    njs_unit_test_req  *r;

    r = (njs_unit_test_req *) obj;
    r->uri = *value;

    return NXT_OK;
}


static njs_ret_t
njs_unit_test_host_external(njs_vm_t *vm, njs_value_t *value, void *obj,
    uintptr_t data)
{
    return njs_string_create(vm, value, (u_char *) "АБВГДЕЁЖЗИЙ", 22, 0);
}


static njs_ret_t
njs_unit_test_header_external(njs_vm_t *vm, njs_value_t *value, void *obj,
    uintptr_t data)
{
    u_char             *s, *p;
    uint32_t           size;
    nxt_str_t          *h;
    njs_unit_test_req  *r;

    r = (njs_unit_test_req *) obj;
    h = (nxt_str_t *) data;

    size = 7 + h->len;

    s = nxt_mem_cache_alloc(r->mem_cache_pool, size);
    if (nxt_slow_path(s == NULL)) {
        return NXT_ERROR;
    }

    p = memcpy(s, h->data, h->len);
    p += h->len;
    *p++ = '|';
    memcpy(p, "АБВ", 6);

    return njs_string_create(vm, value, s, size, 0);
}


static njs_ret_t
njs_unit_test_header_foreach_external(njs_vm_t *vm, void *obj, void *next)
{
    u_char  *s;

    s = next;
    s[0] = '0';
    s[1] = '0';

    return NXT_OK;
}


static njs_ret_t
njs_unit_test_header_next_external(njs_vm_t *vm, njs_value_t *value, void *obj,
    void *next)
{
    u_char  *s;

    s = next;
    s[1]++;

    if (s[1] == '4') {
        return NXT_DONE;
    }

    return njs_string_create(vm, value, s, 2, 0);
}


static njs_ret_t
njs_unit_test_method_external(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    nxt_int_t          ret;
    nxt_str_t          s;
    uintptr_t          next;
    njs_unit_test_req  *r;

    next = 0;

    if (nargs > 1) {

        ret = njs_value_string_copy(vm, &s, njs_argument(args, 1), &next);

        if (ret == NXT_OK && s.len == 3 && memcmp(s.data, "YES", 3) == 0) {
            r = njs_value_data(njs_argument(args, 0));
            njs_vm_return_string(vm, r->uri.data, r->uri.len);

            return NXT_OK;
        }
    }

    return NXT_ERROR;
}


static njs_external_t  njs_unit_test_r_external[] = {

    { nxt_string("uri"),
      NJS_EXTERN_PROPERTY,
      NULL,
      0,
      njs_unit_test_r_get_uri_external,
      njs_unit_test_r_set_uri_external,
      NULL,
      NULL,
      NULL,
      NULL,
      0 },

    { nxt_string("host"),
      NJS_EXTERN_PROPERTY,
      NULL,
      0,
      njs_unit_test_host_external,
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      0 },

    { nxt_string("header"),
      NJS_EXTERN_OBJECT,
      NULL,
      0,
      njs_unit_test_header_external,
      NULL,
      NULL,
      njs_unit_test_header_foreach_external,
      njs_unit_test_header_next_external,
      NULL,
      0 },

    { nxt_string("some_method"),
      NJS_EXTERN_METHOD,
      NULL,
      0,
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      njs_unit_test_method_external,
      0 },

};


static njs_external_t  nxt_test_external[] = {

    { nxt_string("$r"),
      NJS_EXTERN_OBJECT,
      njs_unit_test_r_external,
      nxt_nitems(njs_unit_test_r_external),
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      0 },

};


static nxt_int_t
njs_unit_test_externals(nxt_lvlhsh_t *externals, nxt_mem_cache_pool_t *mcp)
{
    nxt_lvlhsh_init(externals);

    return njs_add_external(externals, mcp, 0, nxt_test_external,
                            nxt_nitems(nxt_test_external));
}


static void *
njs_alloc(void *mem, size_t size)
{
    return nxt_malloc(size);
}


static void *
njs_zalloc(void *mem, size_t size)
{
    void  *p;

    p = nxt_malloc(size);

    if (p != NULL) {
        memset(p, 0, size);
    }

    return p;
}


static void *
njs_align(void *mem, size_t alignment, size_t size)
{
    return nxt_memalign(alignment, size);
}


static void
njs_free(void *mem, void *p)
{
    nxt_free(p);
}


static const nxt_mem_proto_t  njs_mem_cache_pool_proto = {
    njs_alloc,
    njs_zalloc,
    njs_align,
    NULL,
    njs_free,
    NULL,
    NULL,
};


static nxt_int_t
njs_unit_test(nxt_bool_t disassemble)
{
    void                  *ext_object;
    u_char                *start;
    njs_vm_t              *vm, *nvm;
    nxt_int_t             ret;
    nxt_str_t             s, r_name;
    nxt_uint_t            i;
    nxt_bool_t            success;
    nxt_lvlhsh_t          externals;
    njs_function_t        *function;
    njs_vm_shared_t       *shared;
    njs_unit_test_req     r;
    njs_opaque_value_t    value;
    nxt_mem_cache_pool_t  *mcp;

    /*
     * Chatham Islands NZ-CHAT time zone.
     * Standard time: UTC+12:45, Daylight Saving time: UTC+13:45.
     */
    (void) putenv((char *) "TZ=Pacific/Chatham");
    tzset();

    shared = NULL;

    mcp = nxt_mem_cache_pool_create(&njs_mem_cache_pool_proto, NULL, NULL,
                                    2 * nxt_pagesize(), 128, 512, 16);
    if (nxt_slow_path(mcp == NULL)) {
        return NXT_ERROR;
    }

    r.mem_cache_pool = mcp;
    r.uri.len = 6;
    r.uri.data = (u_char *) "АБВ";

    ext_object = &r;

    if (njs_unit_test_externals(&externals, mcp) != NXT_OK) {
        return NXT_ERROR;
    }

    for (i = 0; i < nxt_nitems(njs_test); i++) {

        printf("\"%.*s\"\n",
               (int) njs_test[i].script.len, njs_test[i].script.data);

        vm = njs_vm_create(mcp, &shared, &externals);
        if (vm == NULL) {
            return NXT_ERROR;
        }

        start = njs_test[i].script.data;

        ret = njs_vm_compile(vm, &start, start + njs_test[i].script.len,
                             &function);

        if (ret == NXT_OK) {
            if (disassemble) {
                njs_disassembler(vm);
            }

            nvm = njs_vm_clone(vm, NULL, &ext_object);
            if (nvm == NULL) {
                return NXT_ERROR;
            }

            r.uri.len = 6;
            r.uri.data = (u_char *) "АБВ";

            if (function != NULL) {
                r_name.len = 2;
                r_name.data = (u_char *) "$r";

                ret = njs_external_get(vm, NULL, &r_name, &value);
                if (ret != NXT_OK) {
                    return NXT_ERROR;
                }

                ret = njs_vm_call(nvm, function, &value, 1);

            } else {
                ret = njs_vm_run(nvm);
            }

            if (ret == NXT_OK) {
                if (njs_vm_retval(nvm, &s) != NXT_OK) {
                    return NXT_ERROR;
                }

            } else {
                njs_vm_exception(nvm, &s);
            }

        } else {
            njs_vm_exception(vm, &s);
            nvm = NULL;
        }

        success = nxt_strstr_eq(&njs_test[i].ret, &s);

        if (success) {
            if (nvm != NULL) {
                njs_vm_destroy(nvm);
            }

            continue;
        }

        printf("njs(\"%.*s\") failed: \"%.*s\" vs \"%.*s\"\n",
               (int) njs_test[i].script.len, njs_test[i].script.data,
               (int) njs_test[i].ret.len, njs_test[i].ret.data,
               (int) s.len, s.data);

        return NXT_ERROR;
    }

    nxt_mem_cache_pool_destroy(mcp);

    printf("njs unit tests passed\n");

    return NXT_OK;
}


static nxt_int_t
njs_unit_test_benchmark(nxt_str_t *script, nxt_str_t *result, const char *msg,
    nxt_uint_t n)
{
    void                  *ext_object;
    u_char                *start;
    njs_vm_t              *vm, *nvm;
    uint64_t              us;
    nxt_int_t             ret;
    nxt_str_t             s;
    nxt_uint_t            i;
    nxt_bool_t            success;
    nxt_lvlhsh_t          externals;
    struct rusage         usage;
    njs_vm_shared_t       *shared;
    njs_unit_test_req     r;
    nxt_mem_cache_pool_t  *mcp;

    shared = NULL;

    mcp = nxt_mem_cache_pool_create(&njs_mem_cache_pool_proto, NULL, NULL,
                                    2 * nxt_pagesize(), 128, 512, 16);
    if (nxt_slow_path(mcp == NULL)) {
        return NXT_ERROR;
    }

    r.mem_cache_pool = mcp;
    r.uri.len = 6;
    r.uri.data = (u_char *) "АБВ";

    ext_object = &r;

    if (njs_unit_test_externals(&externals, mcp) != NXT_OK) {
        return NXT_ERROR;
    }

    vm = njs_vm_create(mcp, &shared, &externals);
    if (vm == NULL) {
        return NXT_ERROR;
    }

    start = script->data;

    ret = njs_vm_compile(vm, &start, start + script->len, NULL);
    if (ret != NXT_OK) {
        return NXT_ERROR;
    }

    for (i = 0; i < n; i++) {

        nvm = njs_vm_clone(vm, NULL, &ext_object);
        if (nvm == NULL) {
            return NXT_ERROR;
        }

        if (njs_vm_run(nvm) == NXT_OK) {

            if (njs_vm_retval(nvm, &s) != NXT_OK) {
                return NXT_ERROR;
            }

        } else {
            njs_vm_exception(nvm, &s);
        }

        success = nxt_strstr_eq(result, &s);

        if (!success) {
            return NXT_ERROR;
        }

        njs_vm_destroy(nvm);
    }

    nxt_mem_cache_pool_destroy(mcp);

    getrusage(RUSAGE_SELF, &usage);

    us = usage.ru_utime.tv_sec * 1000000 + usage.ru_utime.tv_usec
         + usage.ru_stime.tv_sec * 1000000 + usage.ru_stime.tv_usec;

    if (n == 1) {
        us /= 1000;
        printf("%s: %d.%03ds\n", msg, (int) (us / 1000), (int) (us % 1000));

    } else {
        printf("%s: %d\n", msg, (int) ((uint64_t) n * 1000000 / us));
    }

    return NXT_OK;
}


int nxt_cdecl
main(int argc, char **argv)
{
    nxt_bool_t  disassemble;

    static nxt_str_t  script = nxt_string("1");
    static nxt_str_t  result = nxt_string("1");

    static nxt_str_t  fibo_number = nxt_string(
        "function fibo(n) {"
        "    if (n > 1)"
        "        return fibo(n - 1) + fibo(n - 2)"
        "    return 1"
        "}"
        "fibo(32)");

    static nxt_str_t  fibo_ascii = nxt_string(
        "function fibo(n) {"
        "    if (n > 1)"
        "        return fibo(n - 1) + fibo(n - 2)"
        "    return '.'"
        "}"
        "fibo(32).length");

    static nxt_str_t  fibo_bytes = nxt_string(
        "function fibo(n) {"
        "    if (n > 1)"
        "        return fibo(n - 1) + fibo(n - 2)"
        "    return '\\x80'.toBytes()"
        "}"
        "fibo(32).length");

    static nxt_str_t  fibo_utf8 = nxt_string(
        "function fibo(n) {"
        "    if (n > 1)"
        "        return fibo(n - 1) + fibo(n - 2)"
        "    return 'α'"
        "}"
        "fibo(32).length");

    static nxt_str_t  fibo_result = nxt_string("3524578");


    disassemble = 0;

    if (argc > 1) {
        switch (argv[1][0]) {

        case 'v':
            return njs_unit_test_benchmark(&script, &result,
                                           "nJSVM clone/destroy", 1000000);

        case 'n':
            return njs_unit_test_benchmark(&fibo_number, &fibo_result,
                                           "fibobench numbers", 1);

        case 'a':
            return njs_unit_test_benchmark(&fibo_ascii, &fibo_result,
                                           "fibobench ascii strings", 1);

        case 'b':
            return njs_unit_test_benchmark(&fibo_bytes, &fibo_result,
                                           "fibobench byte strings", 1);

        case 'u':
            return njs_unit_test_benchmark(&fibo_utf8, &fibo_result,
                                           "fibobench utf8 strings", 1);

        case 'd':
            disassemble = 1;
            break;

        default:
            break;
        }
    }

    return njs_unit_test(disassemble);
}
