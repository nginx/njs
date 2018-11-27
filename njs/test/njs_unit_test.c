
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <njs_core.h>
#include <nxt_lvlhsh.h>
#include <nxt_djb_hash.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/resource.h>
#include <time.h>


typedef struct {
    nxt_str_t  script;
    nxt_str_t  ret;
} njs_unit_test_t;


static njs_unit_test_t  njs_test[] =
{
    { nxt_string("}"),
      nxt_string("SyntaxError: Unexpected token \"}\" in 1") },

    { nxt_string("1}"),
      nxt_string("SyntaxError: Unexpected token \"}\" in 1") },

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
      nxt_string("SyntaxError: Unexpected token \";\" in 1") },

    { nxt_string("var + a"),
      nxt_string("SyntaxError: Unexpected token \"+\" in 1") },

    { nxt_string("//\r\n; var + a"),
      nxt_string("SyntaxError: Unexpected token \"+\" in 2") },

    { nxt_string("/*\n*/; var + a"),
      nxt_string("SyntaxError: Unexpected token \"+\" in 2") },

    { nxt_string("var \n a \n = 1; a"),
      nxt_string("1") },

    { nxt_string("var \n a, \n b; b"),
      nxt_string("undefined") },

    { nxt_string("var a = 1; var b; a"),
      nxt_string("1") },

    { nxt_string("a = 1;for(;a;a--)var a; a"),
      nxt_string("0") },

    { nxt_string("if(1)if(0){0?0:0}else\nvar a\nelse\nvar b"),
      nxt_string("undefined") },

    { nxt_string("var a = 1; var a; a"),
      nxt_string("1") },

    { nxt_string("(function (x) {if (x) { var a = 3; return a} else { var a = 4; return a}})(1)"),
      nxt_string("3") },

    { nxt_string("(function (x) {if (x) { var a = 3; return a} else { var a = 4; return a}})(0)"),
      nxt_string("4") },

    { nxt_string("function f(){return 2}; var f; f()"),
      nxt_string("2") },

    { nxt_string("function f(){return 2}; var f = 1; f()"),
      nxt_string("TypeError: number is not a function") },

    { nxt_string("function f(){return 1}; function f(){return 2}; f()"),
      nxt_string("2") },

    { nxt_string("var f = 1; function f() {}; f"),
      nxt_string("1") },

#if 0 /* TODO */
    { nxt_string("var a; Object.getOwnPropertyDescriptor(this, 'a').value"),
      nxt_string("undefined") },

    { nxt_string("this.a = 1; a"),
      nxt_string("1") },
#endif

    { nxt_string("f() = 1"),
      nxt_string("ReferenceError: Invalid left-hand side in assignment in 1") },

    { nxt_string("f.a() = 1"),
      nxt_string("ReferenceError: Invalid left-hand side in assignment in 1") },

    { nxt_string("++f()"),
      nxt_string("ReferenceError: Invalid left-hand side in prefix operation in 1") },

    { nxt_string("f()++"),
      nxt_string("ReferenceError: Invalid left-hand side in postfix operation in 1") },

    /* Numbers. */

    { nxt_string("0"),
      nxt_string("0") },

    { nxt_string("-0"),
      nxt_string("-0") },

    { nxt_string(".0"),
      nxt_string("0") },

    { nxt_string("0.1"),
      nxt_string("0.1") },

    { nxt_string(".9"),
      nxt_string("0.9") },

    { nxt_string("-.01"),
      nxt_string("-0.01") },

    { nxt_string("0.000001"),
      nxt_string("0.000001") },

    { nxt_string("0.00000123456"),
      nxt_string("0.00000123456") },

    { nxt_string("0.0000001"),
      nxt_string("1e-7") },

    { nxt_string("1.1000000"),
      nxt_string("1.1") },

    { nxt_string("99999999999999999999"),
      nxt_string("100000000000000000000") },

    { nxt_string("99999999999999999999.111"),
      nxt_string("100000000000000000000") },

    { nxt_string("999999999999999999999"),
      nxt_string("1e+21") },

    { nxt_string("9223372036854775808"),
      nxt_string("9223372036854776000") },

    { nxt_string("18446744073709551616"),
      nxt_string("18446744073709552000") },

    { nxt_string("1.7976931348623157E+308"),
      nxt_string("1.7976931348623157e+308") },

    { nxt_string("+1"),
      nxt_string("1") },

    { nxt_string("+1\n"),
      nxt_string("1") },

    { nxt_string("."),
      nxt_string("SyntaxError: Unexpected token \".\" in 1") },

    /* Octal Numbers. */

    { nxt_string("0o0"),
      nxt_string("0") },

    { nxt_string("0O10"),
      nxt_string("8") },

    { nxt_string("0o011"),
      nxt_string("9") },

    { nxt_string("-0O777"),
      nxt_string("-511") },

    { nxt_string("0o"),
      nxt_string("SyntaxError: Unexpected token \"0o\" in 1") },

    { nxt_string("0O778"),
      nxt_string("SyntaxError: Unexpected token \"0O778\" in 1") },

    /* Legacy Octal Numbers are deprecated. */

    { nxt_string("00"),
      nxt_string("SyntaxError: Unexpected token \"00\" in 1") },

    { nxt_string("08"),
      nxt_string("SyntaxError: Unexpected token \"08\" in 1") },

    { nxt_string("09"),
      nxt_string("SyntaxError: Unexpected token \"09\" in 1") },

    { nxt_string("0011"),
      nxt_string("SyntaxError: Unexpected token \"00\" in 1") },

    /* Binary Numbers. */

    { nxt_string("0b0"),
      nxt_string("0") },

    { nxt_string("0B10"),
      nxt_string("2") },

    { nxt_string("0b0101"),
      nxt_string("5") },

    { nxt_string("-0B11111111"),
      nxt_string("-255") },

    { nxt_string("0b"),
      nxt_string("SyntaxError: Unexpected token \"0b\" in 1") },

    { nxt_string("0B12"),
      nxt_string("SyntaxError: Unexpected token \"0B12\" in 1") },

    /* Hex Numbers. */

    { nxt_string("0x0"),
      nxt_string("0") },

    { nxt_string("-0x1"),
      nxt_string("-1") },

    { nxt_string("0xffFF"),
      nxt_string("65535") },

    { nxt_string("0X0000BEEF"),
      nxt_string("48879") },

    { nxt_string("0x"),
      nxt_string("SyntaxError: Unexpected token \"0x\" in 1") },

    { nxt_string("0xffff."),
      nxt_string("SyntaxError: Unexpected token \"\" in 1") },

    { nxt_string("0x12g"),
      nxt_string("SyntaxError: Unexpected token \"g\" in 1") },

    { nxt_string(""),
      nxt_string("undefined") },

    { nxt_string("\n"),
      nxt_string("undefined") },

    { nxt_string(";"),
      nxt_string("undefined") },

    { nxt_string("\n +1"),
      nxt_string("1") },

    /* Scientific notation. */

    { nxt_string("0e0"),
      nxt_string("0") },

    { nxt_string("0.0e0"),
      nxt_string("0") },

    { nxt_string("1e0"),
      nxt_string("1") },

    { nxt_string("1e1"),
      nxt_string("10") },

    { nxt_string("1.e01"),
      nxt_string("10") },

    { nxt_string("5.7e1"),
      nxt_string("57") },

    { nxt_string("5.7e-1"),
      nxt_string("0.57") },

    { nxt_string("-5.7e-1"),
      nxt_string("-0.57") },

    { nxt_string("1.1e-01"),
      nxt_string("0.11") },

    { nxt_string("5.7e-2"),
      nxt_string("0.057") },

    { nxt_string("1.1e+01"),
      nxt_string("11") },

    { nxt_string("-.01e-01"),
      nxt_string("-0.001") },

    { nxt_string("1e9"),
      nxt_string("1000000000") },

    { nxt_string("1.0e308"),
      nxt_string("1e+308") },

    { nxt_string("0e309"),
      nxt_string("0") },

    { nxt_string("0e-309"),
      nxt_string("0") },

    { nxt_string("1e309"),
      nxt_string("Infinity") },

    { nxt_string("-1e309"),
      nxt_string("-Infinity") },

    { nxt_string("1e"),
      nxt_string("SyntaxError: Unexpected token \"e\" in 1") },

    { nxt_string("1.e"),
      nxt_string("SyntaxError: Unexpected token \"e\" in 1") },

    { nxt_string("1e+"),
      nxt_string("SyntaxError: Unexpected token \"e\" in 1") },

    { nxt_string("1.e-"),
      nxt_string("SyntaxError: Unexpected token \"e\" in 1") },

    { nxt_string("1eZ"),
      nxt_string("SyntaxError: Unexpected token \"eZ\" in 1") },

    { nxt_string(".e1"),
      nxt_string("SyntaxError: Unexpected token \".\" in 1") },

    { nxt_string("Number.prototype.X = function(){return 123;};"
                 "(1).X()"),
      nxt_string("123") },

    /* Indexes. */

    { nxt_string("var a = []; a[-1] = 2; a[-1] == a['-1']"),
      nxt_string("true") },

    { nxt_string("var a = []; a[Infinity] = 2; a[Infinity] == a['Infinity']"),
      nxt_string("true") },

    { nxt_string("var a = []; a[NaN] = 2; a[NaN] == a['NaN']"),
      nxt_string("true") },

    /* Number.toString(radix) method. */

    { nxt_string("0..toString(2)"),
      nxt_string("0") },

    { nxt_string("240..toString(2)"),
      nxt_string("11110000") },

    { nxt_string("Math.pow(-2, 1023).toString(2).length"),
      nxt_string("1025") },

    { nxt_string("8.0625.toString(2)"),
      nxt_string("1000.0001") },

    { nxt_string("(1/3).toString(2)"),
      nxt_string("0.010101010101010101010101010101010101010101010101010101") },

    { nxt_string("9999..toString(3)"),
      nxt_string("111201100") },

    { nxt_string("-9999..toString(3)"),
      nxt_string("-111201100") },

    { nxt_string("81985529216486895..toString(16)"),
      nxt_string("123456789abcdf0") },

    { nxt_string("0xffff.toString(16)"),
      nxt_string("ffff") },

    { nxt_string("30520..toString(36)"),
      nxt_string("njs") },

    { nxt_string("Infinity.toString()"),
      nxt_string("Infinity") },

    { nxt_string("Infinity.toString(2)"),
      nxt_string("Infinity") },

    { nxt_string("Infinity.toString(10)"),
      nxt_string("Infinity") },

    { nxt_string("Infinity.toString(NaN)"),
      nxt_string("RangeError") },

    { nxt_string("Infinity.toString({})"),
      nxt_string("RangeError") },

    { nxt_string("Infinity.toString(Infinity)"),
      nxt_string("RangeError") },

    { nxt_string("NaN.toString()"),
      nxt_string("NaN") },

    { nxt_string("NaN.toString(2)"),
      nxt_string("NaN") },

    { nxt_string("NaN.toString(10)"),
      nxt_string("NaN") },

    { nxt_string("NaN.toString(Infinity)"),
      nxt_string("RangeError") },

    { nxt_string("NaN.toString({})"),
      nxt_string("RangeError") },

    { nxt_string("NaN.toString(NaN)"),
      nxt_string("RangeError") },

    /* An object "valueOf/toString" methods. */

    { nxt_string("var a = { valueOf: function() { return 1 } };    +a"),
      nxt_string("1") },

    { nxt_string("var a = { valueOf: function() { return '1' } };  +a"),
      nxt_string("1") },

    { nxt_string("var a = { valueOf: 2,"
                 "          toString: function() { return '1' } }; +a"),
      nxt_string("1") },

    { nxt_string("var a = { valueOf: function() { return 1 } }; ''+a"),
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

    { nxt_string("0xA + ''"),
      nxt_string("10") },

    { nxt_string("undefined + undefined"),
      nxt_string("NaN") },

    { nxt_string("1.2 + 5.7"),
      nxt_string("6.9") },

    { nxt_string("0xf + 1"),
      nxt_string("16") },

    { nxt_string("1 + 1 + '2' + 1 + 1"),
      nxt_string("2211") },

    { nxt_string("'gg' + -0"),
      nxt_string("gg0") },

    { nxt_string("1.2 - '5.7'"),
      nxt_string("-4.5") },

    { nxt_string("1.2 + -'5.7'"),
      nxt_string("-4.5") },

    { nxt_string("1.2 - '-5.7'"),
      nxt_string("6.9") },

    { nxt_string("5 - ' \t 12  \t'"),
      nxt_string("-7") },

    { nxt_string("5 - '12zz'"),
      nxt_string("NaN") },

    { nxt_string("5 - '0x2'"),
      nxt_string("3") },

    { nxt_string("5 - '-0x2'"),
      nxt_string("7") },

    { nxt_string("5 - '\t 0x2 \t'"),
      nxt_string("3") },

    { nxt_string("5 - '0x2 z'"),
      nxt_string("NaN") },

    { nxt_string("12 - '5.7e1'"),
      nxt_string("-45") },

    { nxt_string("12 - '5.e1'"),
      nxt_string("-38") },

    { nxt_string("12 - '5.7e+01'"),
      nxt_string("-45") },

    { nxt_string("12 - '5.7e-01'"),
      nxt_string("11.43") },

    { nxt_string("12 - ' 5.7e1 '"),
      nxt_string("-45") },

    { nxt_string("12 - '5.7e'"),
      nxt_string("NaN") },

    { nxt_string("12 - '5.7e+'"),
      nxt_string("NaN") },

    { nxt_string("12 - '5.7e-'"),
      nxt_string("NaN") },

    { nxt_string("12 - ' 5.7e1 z'"),
      nxt_string("NaN") },

    { nxt_string("5 - '0x'"),
      nxt_string("NaN") },

    { nxt_string("1 + +'3'"),
      nxt_string("4") },

    { nxt_string("1 - undefined"),
      nxt_string("NaN") },

    { nxt_string("1 - ''"),
      nxt_string("1") },

    { nxt_string("undefined - undefined"),
      nxt_string("NaN") },

    /* String.toString() method. */

    { nxt_string("'A'.toString()"),
      nxt_string("A") },

    { nxt_string("'A'.toString('hex')"),
      nxt_string("TypeError: argument must be a byte string") },

    { nxt_string("'A'.toBytes().toString('latin1')"),
      nxt_string("TypeError: Unknown encoding: 'latin1'") },

    { nxt_string("'ABCD'.toBytes().toString('hex')"),
      nxt_string("41424344") },

    { nxt_string("'\\x00\\xAA\\xBB\\xFF'.toBytes().toString('hex')"),
      nxt_string("00aabbff") },

    { nxt_string("'\\x00\\xAA\\xBB\\xFF'.toBytes().toString('base64')"),
      nxt_string("AKq7/w==") },

    { nxt_string("'ABCD'.toBytes().toString('base64')"),
      nxt_string("QUJDRA==") },

    { nxt_string("'ABC'.toBytes().toString('base64')"),
      nxt_string("QUJD") },

    { nxt_string("'AB'.toBytes().toString('base64')"),
      nxt_string("QUI=") },

    { nxt_string("'A'.toBytes().toString('base64')"),
      nxt_string("QQ==") },

    { nxt_string("''.toBytes().toString('base64')"),
      nxt_string("") },

    { nxt_string("'\\x00\\xAA\\xBB\\xFF'.toBytes().toString('base64url')"),
      nxt_string("AKq7_w") },

    { nxt_string("'ABCD'.toBytes().toString('base64url')"),
      nxt_string("QUJDRA") },

    { nxt_string("'ABC'.toBytes().toString('base64url')"),
      nxt_string("QUJD") },

    { nxt_string("'AB'.toBytes().toString('base64url')"),
      nxt_string("QUI") },

    { nxt_string("'A'.toBytes().toString('base64url')"),
      nxt_string("QQ") },

    { nxt_string("''.toBytes().toString('base64url')"),
      nxt_string("") },

    /* Assignment. */

    { nxt_string("var a, b = (a = [2]) * (3 * 4); a +' '+ b"),
      nxt_string("2 24") },

    /* 3 address operation and side effect. */

    { nxt_string("var a = 1; function f(x) { a = x; return 2 }; a+f(5)+' '+a"),
      nxt_string("3 5") },

    { nxt_string("var a = 1; function f(x) { a = x; return 2 }; a += f(5)"),
      nxt_string("3") },

    { nxt_string("var x; x in (x = 1, [1, 2, 3])"),
      nxt_string("false") },

    /* Exponentiation. */

    { nxt_string("2 ** 3 ** 2"),
      nxt_string("512") },

    { nxt_string("2 ** (3 ** 2)"),
      nxt_string("512") },

    { nxt_string("(2 ** 3) ** 2"),
      nxt_string("64") },

    { nxt_string("3 ** 2 - 9"),
      nxt_string("0") },

    { nxt_string("-9 + 3 ** 2"),
      nxt_string("0") },

    { nxt_string("-3 ** 2"),
      nxt_string("SyntaxError: Either left-hand side or entire exponentiation "
                 "must be parenthesized in 1") },

    { nxt_string("-(3) ** 2"),
      nxt_string("SyntaxError: Either left-hand side or entire exponentiation "
                 "must be parenthesized in 1") },

    { nxt_string("-(3 ** 2)"),
      nxt_string("-9") },

    { nxt_string("(-3) ** 2"),
      nxt_string("9") },

    { nxt_string("1 ** NaN"),
      nxt_string("NaN") },

    { nxt_string("'a' ** -0"),
      nxt_string("1") },

    { nxt_string("1.1 ** Infinity"),
      nxt_string("Infinity") },

    { nxt_string("(-1.1) ** -Infinity"),
      nxt_string("0") },

    { nxt_string("(-1) ** Infinity"),
      nxt_string("NaN") },

    { nxt_string("1 ** -Infinity"),
      nxt_string("NaN") },

    { nxt_string("(-0.9) ** Infinity"),
      nxt_string("0") },

    { nxt_string("0.9 ** -Infinity"),
      nxt_string("Infinity") },

    { nxt_string("'Infinity' ** 0.1"),
      nxt_string("Infinity") },

    { nxt_string("Infinity ** '-0.1'"),
      nxt_string("0") },

    { nxt_string("(-Infinity) ** 3"),
      nxt_string("-Infinity") },

    { nxt_string("'-Infinity' ** '3.1'"),
      nxt_string("Infinity") },

    { nxt_string("(-Infinity) ** '-3'"),
      nxt_string("-0") },

    { nxt_string("'-Infinity' ** -2"),
      nxt_string("0") },

    { nxt_string("'0' ** 0.1"),
      nxt_string("0") },

#ifndef __NetBSD__  /* NetBSD 7: pow(0, negative) == -Infinity. */
    { nxt_string("0 ** '-0.1'"),
      nxt_string("Infinity") },
#endif

    { nxt_string("(-0) ** 3"),
      nxt_string("-0") },

    { nxt_string("'-0' ** '3.1'"),
      nxt_string("0") },

    { nxt_string("(-0) ** '-3'"),
      nxt_string("-Infinity") },

#ifndef __NetBSD__  /* NetBSD 7: pow(0, negative) == -Infinity. */
    { nxt_string("'-0' ** -2"),
      nxt_string("Infinity") },
#endif

    { nxt_string("(-3) ** 0.1"),
      nxt_string("NaN") },

    { nxt_string("var a = 0.1; a **= -2"),
      nxt_string("99.99999999999999") },

    { nxt_string("var a = 1; a **= NaN"),
      nxt_string("NaN") },

    { nxt_string("var a = 'a'; a **= -0"),
      nxt_string("1") },

    { nxt_string("var a = 1.1; a **= Infinity"),
      nxt_string("Infinity") },

    { nxt_string("var a = -1.1; a **= -Infinity"),
      nxt_string("0") },

    { nxt_string("var a = -1; a **= Infinity"),
      nxt_string("NaN") },

    { nxt_string("var a = 1; a **= -Infinity"),
      nxt_string("NaN") },

    { nxt_string("var a = -0.9; a **= Infinity"),
      nxt_string("0") },

    { nxt_string("var a = 0.9; a **= -Infinity"),
      nxt_string("Infinity") },

    { nxt_string("var a = 'Infinity'; a **= 0.1"),
      nxt_string("Infinity") },

    { nxt_string("var a = Infinity; a **= '-0.1'"),
      nxt_string("0") },

    { nxt_string("var a = -Infinity; a **= 3"),
      nxt_string("-Infinity") },

    { nxt_string("var a = '-Infinity'; a **= '3.1'"),
      nxt_string("Infinity") },

    { nxt_string("var a = -Infinity; a **= '-3'"),
      nxt_string("-0") },

    { nxt_string("var a = '-Infinity'; a **= -2"),
      nxt_string("0") },

    { nxt_string("var a = '0'; a **= 0.1"),
      nxt_string("0") },

#ifndef __NetBSD__  /* NetBSD 7: pow(0, negative) == -Infinity. */
    { nxt_string("var a = 0; a **= '-0.1'"),
      nxt_string("Infinity") },
#endif

    { nxt_string("var a = -0; a **= 3"),
      nxt_string("-0") },

    { nxt_string("var a = '-0'; a **= '3.1'"),
      nxt_string("0") },

    { nxt_string("var a = -0; a **= '-3'"),
      nxt_string("-Infinity") },

#ifndef __NetBSD__  /* NetBSD 7: pow(0, negative) == -Infinity. */
    { nxt_string("var a = '-0'; a **= -2"),
      nxt_string("Infinity") },
#endif

    { nxt_string("var a = -3; a **= 0.1"),
      nxt_string("NaN") },

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

    { nxt_string("var x; x = 0 || x; x"),
      nxt_string("undefined") },

    { nxt_string("var x; x = 1 && x; x"),
      nxt_string("undefined") },

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

    { nxt_string("var a = true; a = -~!a"),
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

    { nxt_string("var x = '1'; +x + 2"),
      nxt_string("3") },

    /* Weird things. */

    { nxt_string("'3' -+-+-+ '1' + '1' / '3' * '6' + '2'"),
      nxt_string("42") },

    { nxt_string("((+!![])+(+!![])+(+!![])+(+!![])+[])+((+!![])+(+!![])+[])"),
      nxt_string("42") },

    { nxt_string("1+[[]+[]]-[]+[[]-[]]-1"),
      nxt_string("9") },

    { nxt_string("[[]+[]]-[]+[[]-[]]"),
      nxt_string("00") },

    { nxt_string("!--[][1]"),
      nxt_string("true") },

    { nxt_string("[].concat[1,2,3]"),
      nxt_string("undefined") },

    /**/

    { nxt_string("'true' == true"),
      nxt_string("false") },

    { nxt_string("null == false"),
      nxt_string("false") },

    { nxt_string("0 == null"),
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

    { nxt_string("new Number(1) == new String('1')"),
      nxt_string("false") },

    { nxt_string("var a = Object; a == Object"),
      nxt_string("true") },

    { nxt_string("'1' == new Number(1)"),
      nxt_string("true") },

    { nxt_string("new String('abc') == 'abc'"),
      nxt_string("true") },

    { nxt_string("false == new String('0')"),
      nxt_string("true") },

    { nxt_string("var a = { valueOf: function() { return 5 } };   a == 5"),
      nxt_string("true") },

    { nxt_string("var a = { valueOf: function() { return '5' } }; a == 5"),
      nxt_string("true") },

    { nxt_string("var a = { valueOf: function() { return '5' } }; a == '5'"),
      nxt_string("true") },

    { nxt_string("var a = { valueOf: function() { return 5 } }; a == '5'"),
      nxt_string("true") },

    { nxt_string("var a = { toString: function() { return true } }; '1' == a"),
      nxt_string("true") },

    { nxt_string("var a = { valueOf: function() { return 'b' },"
                 "          toString: function() { return 'a' } }; a == 'a'"),
      nxt_string("false") },

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

    { nxt_string("var a = { valueOf: function() { return 1 } };     null < a"),
      nxt_string("true") },

    { nxt_string("var a = { valueOf: function() { return 'null' } };null < a"),
      nxt_string("false") },

    { nxt_string("var a = { valueOf: function() { return '1' } };   null < a"),
      nxt_string("true") },

    /**/

    { nxt_string("undefined == undefined"),
      nxt_string("true") },

    { nxt_string("undefined != undefined"),
      nxt_string("false") },

    { nxt_string("undefined === undefined"),
      nxt_string("true") },

    { nxt_string("undefined !== undefined"),
      nxt_string("false") },

    { nxt_string("undefined < undefined"),
      nxt_string("false") },

    { nxt_string("undefined < null"),
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

    { nxt_string("var a = { valueOf: function() { return 1 } }; undefined < a"),
      nxt_string("false") },

    { nxt_string("var a = { valueOf: function() { return 'undefined' } };"
                 "undefined < a"),
      nxt_string("false") },

    { nxt_string("var a = { valueOf: function() { return '1' } };"
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
    { nxt_string("new String('1') > new Number(1)"),
      nxt_string("false") },

    { nxt_string("new Boolean(true) > '1'"),
      nxt_string("false") },

    { nxt_string("'0' >= new Number(1)"),
      nxt_string("false") },

    { nxt_string("'1' >= new Number(1)"),
      nxt_string("true") },

    { nxt_string("new String('1') < new Number(1)"),
      nxt_string("false") },

    { nxt_string("new Boolean(true) < '1'"),
      nxt_string("false") },

    { nxt_string("new String('1') <= new Number(1)"),
      nxt_string("true") },

    { nxt_string("new Boolean(true) <= '1'"),
      nxt_string("true") },

    { nxt_string("'-1' < {valueOf: function() {return -2}}"),
      nxt_string("false") },

    /**/

    { nxt_string("var a; a = 1 ? 2 : 3"),
      nxt_string("2") },

    { nxt_string("var a; a = 1 ? 2 : 3 ? 4 : 5"),
      nxt_string("2") },

    { nxt_string("var a; a = 0 ? 2 : 3 ? 4 : 5"),
      nxt_string("4") },

    { nxt_string("0 ? 2 ? 3 : 4 : 5"),
      nxt_string("5") },

    { nxt_string("1 ? 2 ? 3 : 4 : 5"),
      nxt_string("3") },

    { nxt_string("1 ? 0 ? 3 : 4 : 5"),
      nxt_string("4") },

    { nxt_string("(1 ? 0 : 3) ? 4 : 5"),
      nxt_string("5") },

    { nxt_string("var a; a = (1 + 2) ? 2 ? 3 + 4 : 5 : 6"),
      nxt_string("7") },

    { nxt_string("var a; a = (1 ? 2 : 3) + 4"),
      nxt_string("6") },

    { nxt_string("var a, b; a = 1 ? b = 2 + 4 : b = 3"),
      nxt_string("6") },

    { nxt_string("var a; a = 1 ? [1,2] : []"),
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

    { nxt_string("var a = { valueOf: function() { return 1 } };   a = ++a"),
      nxt_string("2") },

    { nxt_string("var a = { valueOf: function() { return '1' } }; a = ++a"),
      nxt_string("2") },

    { nxt_string("var a = { valueOf: function() { return [1] } }; a = ++a"),
      nxt_string("NaN") },

    { nxt_string("var a = { valueOf: function() { return {} } };  a = ++a"),
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

    { nxt_string("var a = { valueOf: function() { return 1 } };"
                 "var b = ++a; a +' '+ b"),
      nxt_string("2 2") },

    { nxt_string("var a = { valueOf: function() { return '1' } };"
                 "var b = ++a; a +' '+ b"),
      nxt_string("2 2") },

    { nxt_string("var a = { valueOf: function() { return [1] } };"
                 "var b = ++a; a +' '+ b"),
      nxt_string("NaN NaN") },

    { nxt_string("var a = { valueOf: function() { return {} } };"
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

    { nxt_string("var a = { valueOf: function() { return 1 } };   a++"),
      nxt_string("1") },

    { nxt_string("var a = { valueOf: function() { return '1' } }; a++"),
      nxt_string("1") },

    { nxt_string("var a = { valueOf: function() { return [1] } }; a++"),
      nxt_string("NaN") },

    { nxt_string("var a = { valueOf: function() { return {} } };  a++"),
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

    { nxt_string("var a = { valueOf: function() { return 1 } };   a = a++"),
      nxt_string("1") },

    { nxt_string("var a = { valueOf: function() { return '1' } }; a = a++"),
      nxt_string("1") },

    { nxt_string("var a = { valueOf: function() { return [1] } }; a = a++"),
      nxt_string("NaN") },

    { nxt_string("var a = { valueOf: function() { return {} } };  a = a++"),
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

    { nxt_string("var a = { valueOf: function() { return 1 } };"
                 "var b = a++; a +' '+ b"),
      nxt_string("2 1") },

    { nxt_string("var a = { valueOf: function() { return '1' } };"
                 "var b = a++; a +' '+ b"),
      nxt_string("2 1") },

    { nxt_string("var a = { valueOf: function() { return [1] } };"
                 "var b = a++; a +' '+ b"),
      nxt_string("NaN NaN") },

    { nxt_string("var a = { valueOf: function() { return {} } };"
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

    { nxt_string("var a = { valueOf: function() { return 1} };   a = --a"),
      nxt_string("0") },

    { nxt_string("var a = { valueOf: function() { return '1'} }; a = --a"),
      nxt_string("0") },

    { nxt_string("var a = { valueOf: function() { return [1]} }; a = --a"),
      nxt_string("NaN") },

    { nxt_string("var a = { valueOf: function() { return {} } }; a = --a"),
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

    { nxt_string("var a = { valueOf: function() { return 1 } };"
                 "var b = --a; a +' '+ b"),
      nxt_string("0 0") },

    { nxt_string("var a = { valueOf: function() { return '1' } };"
                 "var b = --a; a +' '+ b"),
      nxt_string("0 0") },

    { nxt_string("var a = { valueOf: function() { return [1] } };"
                 "var b = --a; a +' '+ b"),
      nxt_string("NaN NaN") },

    { nxt_string("var a = { valueOf: function() { return {} } };"
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

    { nxt_string("var a = { valueOf: function() { return 1 } };   a--"),
      nxt_string("1") },

    { nxt_string("var a = { valueOf: function() { return '1' } }; a--"),
      nxt_string("1") },

    { nxt_string("var a = { valueOf: function() { return [1] } }; a--"),
      nxt_string("NaN") },

    { nxt_string("var a = { valueOf: function() { return {} } };  a--"),
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

    { nxt_string("var a = { valueOf: function() { return 1 } };   a = a--"),
      nxt_string("1") },

    { nxt_string("var a = { valueOf: function() { return '1' } }; a = a--"),
      nxt_string("1") },

    { nxt_string("var a = { valueOf: function() { return [1] } }; a = a--"),
      nxt_string("NaN") },

    { nxt_string("var a = { valueOf: function() { return {} } };  a = a--"),
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

    { nxt_string("var a = { valueOf: function() { return 1 } };"
                 "var b = a--; a +' '+ b"),
      nxt_string("0 1") },

    { nxt_string("var a = { valueOf: function() { return '1' } };"
                 "var b = a--; a +' '+ b"),
      nxt_string("0 1") },

    { nxt_string("var a = { valueOf: function() { return [1] } };"
                 "var b = a--; a +' '+ b"),
      nxt_string("NaN NaN") },

    { nxt_string("var a = { valueOf: function() { return {} } };"
                 "var b = a--; a +' '+ b"),
      nxt_string("NaN NaN") },

    /**/

    { nxt_string("var a, b; a = 2; b = ++a + ++a; a + ' ' + b"),
      nxt_string("4 7") },

    { nxt_string("var a, b; a = 2; b = a++ + a++; a + ' ' + b"),
      nxt_string("4 5") },

    { nxt_string("var a, b; a = b = 7; a +' '+ b"),
      nxt_string("7 7") },

    { nxt_string("var a, b, c; a = b = c = 5; a +' '+ b +' '+ c"),
      nxt_string("5 5 5") },

    { nxt_string("var a, b, c; a = b = (c = 5) + 2; a +' '+ b +' '+ c"),
      nxt_string("7 7 5") },

    { nxt_string("1, 2 + 5, 3"),
      nxt_string("3") },

    { nxt_string("var a, b; a = 1 /* YES */\n b = a + 2 \n \n + 1 \n + 3"),
      nxt_string("7") },

    { nxt_string("var a, b; a = 1 // YES \n b = a + 2 \n \n + 1 \n + 3"),
      nxt_string("7") },

    { nxt_string("var a; a = 0; ++ \n a"),
      nxt_string("1") },

    { nxt_string("a = 0; a \n ++"),
      nxt_string("SyntaxError: Unexpected end of input in 2") },

    { nxt_string("a = 0; a \n --"),
      nxt_string("SyntaxError: Unexpected end of input in 2") },

    { nxt_string("var a = 0; a \n + 1"),
      nxt_string("1") },

    { nxt_string("var a = 0; a \n +\n 1"),
      nxt_string("1") },

    { nxt_string("var a; a = 1 ? 2 \n : 3"),
      nxt_string("2") },

    { nxt_string("var a, b, c;"
                 "a = 0 / 0; b = 1 / 0; c = -1 / 0; a +' '+ b +' '+ c"),
      nxt_string("NaN Infinity -Infinity") },

    { nxt_string("var a, b; a = (b = 7) + 5; var c; a +' '+ b +' '+ c"),
      nxt_string("12 7 undefined") },

    { nxt_string("var a, b = 1, c; a +' '+ b +' '+ c"),
      nxt_string("undefined 1 undefined") },

    { nxt_string("var a = 1, b = a + 1; a +' '+ b"),
      nxt_string("1 2") },

    { nxt_string("var a; a = a = 1"),
      nxt_string("1") },

    { nxt_string("var a = 1, \n b; a +' '+ b"),
      nxt_string("1 undefined") },

    { nxt_string("var a; a = b + 1; var b; a +' '+ b"),
      nxt_string("NaN undefined") },

    { nxt_string("var a += 1"),
      nxt_string("SyntaxError: Unexpected token \"+=\" in 1") },

    { nxt_string("var a = a + 1"),
      nxt_string("undefined") },

    { nxt_string("var a; a = b + 1; var b = 1; a +' '+ b"),
      nxt_string("NaN 1") },

    { nxt_string("var a; (a) = 1"),
      nxt_string("1") },

    { nxt_string("a"),
      nxt_string("ReferenceError: \"a\" is not defined in 1") },

    { nxt_string("a + a"),
      nxt_string("ReferenceError: \"a\" is not defined in 1") },

    { nxt_string("a = b + 1"),
      nxt_string("ReferenceError: \"a\" is not defined in 1") },

    { nxt_string("a = a + 1"),
      nxt_string("ReferenceError: \"a\" is not defined in 1") },

    { nxt_string("a += 1"),
      nxt_string("ReferenceError: \"a\" is not defined in 1") },

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

    { nxt_string("var a = b = 1; var b; a +' '+ b"),
      nxt_string("1 1") },

    { nxt_string("var a \n if (!a) a = 3; a"),
      nxt_string("3") },

    /* automatic semicolon insertion. */

    { nxt_string("var x = 0, y = 2; x\n--\ny; [x,y]"),
      nxt_string("0,1") },

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

    { nxt_string("var a = [3], b; if (1==1||2==2) { b = '1'+'2'+a[0] }; b"),
      nxt_string("123") },

    { nxt_string("(function(){ if(true) return 1 else return 0; })()"),
      nxt_string("SyntaxError: Unexpected token \"else\" in 1") },

    { nxt_string("(function(){ if(true) return 1; else return 0; })()"),
      nxt_string("1") },

    { nxt_string("(function(){ if(true) return 1;; else return 0; })()"),
      nxt_string("SyntaxError: Unexpected token \"else\" in 1") },

    { nxt_string("(function(){ if(true) return 1\n else return 0; })()"),
      nxt_string("1") },

    { nxt_string("(function(){ if(true) return 1\n;\n else return 0; })()"),
      nxt_string("1") },

    { nxt_string("function f(n) {if (n) throw 'foo' else return 1}; f(0)"),
      nxt_string("SyntaxError: Unexpected token \"else\" in 1") },

    { nxt_string("function f(n) {if (n)\n throw 'foo'\nelse return 1}; f(0)"),
      nxt_string("1") },

    { nxt_string("function f(n) {if (n)\n throw 'foo'\nelse return 1}; f(1)"),
      nxt_string("foo") },

    { nxt_string("function f(n) {if (n == 1) throw 'foo'\nelse if (n == 2) return 1}; f(2)"),
      nxt_string("1") },

    { nxt_string("(function(){ for (var p in [1] ){ if (1) break else return 0; }})()"),
      nxt_string("SyntaxError: Unexpected token \"else\" in 1") },

    { nxt_string("(function(){ for (var p in [1] ){ if (1) break\n else return 0; }})()"),
      nxt_string("undefined") },

    { nxt_string("(function(){ for (var p in [1] ){ if (1) break; else return 0; }})()"),
      nxt_string("undefined") },

    { nxt_string("(function(){ for (var p in [1] ){ if (1) continue else return 0; }})()"),
      nxt_string("SyntaxError: Unexpected token \"else\" in 1") },

    { nxt_string("(function(){ for (var p in [1] ){ if (1) continue\n else return 0; }})()"),
      nxt_string("undefined") },

    { nxt_string("(function(){ for (var p in [1] ){ if (1) continue; else return 0; }})()"),
      nxt_string("undefined") },

    { nxt_string("(function(x){ if\n(x) return -1; else return 0; })(0)"),
      nxt_string("0") },

    { nxt_string("(function(x){ if\n(\nx) return -1; else return 0; })(0)"),
      nxt_string("0") },

    { nxt_string("(function(x){ if\n(\nx)\nreturn -1; else return 0; })(0)"),
      nxt_string("0") },

    { nxt_string("(function(x){ if\n(\nx)\nreturn -1\n else return 0; })(0)"),
      nxt_string("0") },

    { nxt_string("(function(x){ if\n(\nx)\nreturn -1\n else\nreturn 0; })(0)"),
      nxt_string("0") },

    /* do while. */

    { nxt_string("do { break } if (false)"),
      nxt_string("SyntaxError: Unexpected token \"if\" in 1") },

    /* for in. */

    { nxt_string("for (null in undefined);"),
      nxt_string("ReferenceError: Invalid left-hand side \"null\" in for-in statement in 1") },

    { nxt_string("var s = ''; for (var p in [1,2]) {s += p}; s"),
      nxt_string("01") },

    { nxt_string("var s = ''; for (var p in {a:1, b:2}) {s += p}; s"),
      nxt_string("ab") },

    { nxt_string("var s = '';"
                 "var o = Object.defineProperty({}, 'x', {value:1});"
                 "Object.defineProperty(o, 'y', {value:2, enumerable:true});"
                 "for (var p in o) {s += p}; s"),
      nxt_string("y") },

    { nxt_string("var o = {a:1, b:2}; var arr = []; "
                 "for (var a in o) {arr.push(a)}; arr"),
      nxt_string("a,b") },

    { nxt_string("var o = {a:1, b:2}; var arr = []; delete o.a; "
                 "for (var a in o) {arr.push(a)}; arr"),
      nxt_string("b") },

    /* switch. */

    { nxt_string("switch"),
      nxt_string("SyntaxError: Unexpected end of input in 1") },

    { nxt_string("switch (1);"),
      nxt_string("SyntaxError: Unexpected token \";\" in 1") },

    { nxt_string("switch (1) { do { } while (1) }"),
      nxt_string("SyntaxError: Unexpected token \"do\" in 1") },

    { nxt_string("switch (1) {}"),
      nxt_string("undefined") },

    { nxt_string("switch (1) {default:}"),
      nxt_string("undefined") },

    { nxt_string("switch (1) {case 0:}"),
      nxt_string("undefined") },

    { nxt_string("switch (1) {default:;}"),
      nxt_string("undefined") },

    { nxt_string("switch (1) {default:; default:}"),
      nxt_string("SyntaxError: More than one default clause in switch statement in 1") },

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
      nxt_string("SyntaxError: Illegal continue statement in 1") },

    { nxt_string("do continue while (false)"),
      nxt_string("SyntaxError: Unexpected token \"while\" in 1") },

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

    { nxt_string("var i; for (i = 0; i < 100; i++) if (i > 9) continue; i"),
      nxt_string("100") },

    { nxt_string("var a = [], i; for (i in a) continue"),
      nxt_string("undefined") },

    { nxt_string("var a = [], i; for (i in a) continue;"),
      nxt_string("undefined") },

    { nxt_string("var a = [], i; for (i in a) { continue }"),
      nxt_string("undefined") },

    { nxt_string("var a = [1,2,3,4,5]; var s = 0, i;"
                 "for (i in a) { if (a[i] > 4) continue; else s += a[i] } s"),
      nxt_string("10") },

    { nxt_string("var a = [1,2,3,4,5]; var s = 0, i;"
                 "for (i in a) { if (a[i] > 4) continue; s += a[i] } s"),
      nxt_string("10") },

    { nxt_string("var a; for (a = 1; a; a--) switch (a) { case 0: continue }"),
      nxt_string("undefined") },

    { nxt_string("var a = [1,2,3], i; for (i in a) {Object.seal({})}"),
      nxt_string("undefined") },

    { nxt_string("var i; for (i in [1,2,3]) {Object.seal({});}"),
      nxt_string("undefined") },

    /* break. */

    { nxt_string("break"),
      nxt_string("SyntaxError: Illegal break statement in 1") },

    { nxt_string("do break while (true)"),
      nxt_string("SyntaxError: Unexpected token \"while\" in 1") },

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

    { nxt_string("var i; for (i = 0; i < 100; i++) if (i > 9) break; i"),
      nxt_string("10") },

    { nxt_string("var a = [], i; for (i in a) break"),
      nxt_string("undefined") },

    { nxt_string("var a = [], i; for (i in a) break;"),
      nxt_string("undefined") },

    { nxt_string("var a = [], i; for (i in a) { break }"),
      nxt_string("undefined") },

    { nxt_string("var a = [1,2,3,4,5]; var s = 0, i;"
                 "for (i in a) { if (a[i] > 4) break; else s += a[i] } s"),
      nxt_string("10") },

    { nxt_string("var a = [1,2,3,4,5]; var s = 0, i;"
                 "for (i in a) { if (a[i] > 4) break; s += a[i] } s"),
      nxt_string("10") },

    { nxt_string("var a = [1,2,3,4,5]; var s = 0, i;"
                 "for (i in a) if (a[i] > 4) break; s += a[i]; s"),
      nxt_string("5") },

    /**/

    { nxt_string("var i; for (i = 0; i < 10; i++) { i += 1 } i"),
      nxt_string("10") },

    /* Factorial. */

    { nxt_string("var n = 5, f = 1; while (n--) f *= n + 1; f"),
      nxt_string("120") },

    { nxt_string("var n = 5, f = 1; while (n) { f *= n; n-- } f"),
      nxt_string("120") },

    /* Fibonacci. */

    { nxt_string("var n = 50, x, i, j, k;"
                 "for(i=0,j=1,k=0; k<n; i=j,j=x,k++ ){ x=i+j } x"),
      nxt_string("20365011074") },

    { nxt_string("3 + 'abc' + 'def' + null + true + false + undefined"),
      nxt_string("3abcdefnulltruefalseundefined") },

    { nxt_string("var a = 0; do a++; while (a < 5) if (a == 5) a = 7.33 \n"
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

    { nxt_string("typeof Date.prototype"),
      nxt_string("object") },

    { nxt_string("typeof a"),
      nxt_string("undefined") },

    { nxt_string("typeof a; var a"),
      nxt_string("undefined") },

    { nxt_string("typeof a; var a; a"),
      nxt_string("undefined") },

    { nxt_string("var a = 5; typeof a"),
      nxt_string("number") },

    { nxt_string("function f() { return typeof a } ; f()"),
      nxt_string("undefined") },

    { nxt_string("typeof a; a"),
      nxt_string("ReferenceError: \"a\" is not defined in 1") },

    { nxt_string("typeof a; a = 1"),
      nxt_string("ReferenceError: \"a\" is not defined in 1") },

    /**/

    { nxt_string("void 0"),
      nxt_string("undefined") },

    { nxt_string("null = 1"),
      nxt_string("ReferenceError: Invalid left-hand side in assignment in 1") },

    { nxt_string("undefined = 1"),
      nxt_string("ReferenceError: Invalid left-hand side in assignment in 1") },

    { nxt_string("null++"),
      nxt_string("ReferenceError: Invalid left-hand side in postfix operation in 1") },

    { nxt_string("++null"),
      nxt_string("ReferenceError: Invalid left-hand side in prefix operation in 1") },

    { nxt_string("var a, b; b = a; a = 1; a +' '+ b"),
      nxt_string("1 undefined") },

    { nxt_string("a = 1"),
      nxt_string("ReferenceError: \"a\" is not defined in 1") },

    { nxt_string("var a; a = 1; a"),
      nxt_string("1") },

    { nxt_string("var a = {}; typeof a +' '+ a"),
      nxt_string("object [object Object]") },

    { nxt_string("var a = {}; a.b"),
      nxt_string("undefined") },

    { nxt_string("var a = {}; a.b = 1 + 2; a.b"),
      nxt_string("3") },

    { nxt_string("var a = {}; a['b']"),
      nxt_string("undefined") },

    { nxt_string("var a = {}; a.b.c"),
      nxt_string("TypeError: cannot get property 'c' of undefined") },

    { nxt_string("'a'[0]"),
      nxt_string("a") },

    { nxt_string("'a'[undefined]"),
      nxt_string("undefined") },

    { nxt_string("'a'[null]"),
      nxt_string("undefined") },

    { nxt_string("'a'[false]"),
      nxt_string("undefined") },

    { nxt_string("'a'.b = 1"),
      nxt_string("TypeError: property set on primitive string type") },

    { nxt_string("'a'[2] = 1"),
      nxt_string("TypeError: property set on primitive string type") },

    { nxt_string("var a = {}; a.b = 1; a.b"),
      nxt_string("1") },

    { nxt_string("var a = {}; a.b = 1; a.b += 2"),
      nxt_string("3") },

    { nxt_string("var a = {}; a.b = 1; a.b += a.b"),
      nxt_string("2") },

    { nxt_string("var a = {}; a.b = 1; var x = {}; x.b = 3; a.b += (x.b = 2)"),
      nxt_string("3") },

    { nxt_string("var a = {}; a.b = 1; a.b += (a.b = 2)"),
      nxt_string("3") },

    { nxt_string("var a = {}; a.b += 1"),
      nxt_string("NaN") },

    { nxt_string("var a = 1; var b = 2; a = b += 1"),
      nxt_string("3") },

    { nxt_string("var a = 1; var b = { x:2 }; a = b.x += 1"),
      nxt_string("3") },

    { nxt_string("var a = 1; var b = { x:2 }; a = b.x += (a = 1)"),
      nxt_string("3") },

    { nxt_string("var a = undefined; a.b++; a.b"),
      nxt_string("TypeError: cannot get property 'b' of undefined") },

    { nxt_string("var a = null; a.b++; a.b"),
      nxt_string("TypeError: cannot get property 'b' of undefined") },

    { nxt_string("var a = true; a.b++; a.b"),
      nxt_string("TypeError: property set on primitive boolean type") },

    { nxt_string("var a = 1; a.b++; a.b"),
      nxt_string("TypeError: property set on primitive number type") },

    { nxt_string("var n = 1, o = { p: n += 1 }; o.p"),
      nxt_string("2") },

    { nxt_string("var a = {}; a.b = {}; a.b.c = 1; a.b['c']"),
      nxt_string("1") },

    { nxt_string("var a = {}; a.b = {}; a.b.c = 1; a['b']['c']"),
      nxt_string("1") },

    { nxt_string("var a = {}; a.b = {}; var c = 'd'; a.b.d = 1; a['b'][c]"),
      nxt_string("1") },

    { nxt_string("var a = {}; a.b = 1; var c = a.b++; a.b +' '+ c"),
      nxt_string("2 1") },

    { nxt_string("var a = 2; a.b = 1; var c = a.b++; a +' '+ a.b +' '+ c"),
      nxt_string("TypeError: property set on primitive number type") },

    { nxt_string("var x = { a: 1 }; x.a"),
      nxt_string("1") },

    { nxt_string("var a = { x:1 }; var b = { y:2 }; a.x = b.y; a.x"),
      nxt_string("2") },

    { nxt_string("var a = { x:1 }; var b = { y:2 }; var c; c = a.x = b.y"),
      nxt_string("2") },

    { nxt_string("var a = { x:1 }; var b = { y:2 }; var c = a.x = b.y; c"),
      nxt_string("2") },

    { nxt_string("var a = { x:1 }; var b = { y:2 }; a.x = b.y"),
      nxt_string("2") },

    { nxt_string("var a = { x:1 }; var b = a.x = 1 + 2; a.x +' '+ b"),
      nxt_string("3 3") },

    { nxt_string("var a = { x:1 }; var b = { y:2 }; var c = {};"
                 "c.x = a.x = b.y; c.x"),
      nxt_string("2") },

    { nxt_string("var y = 2, x = { a:1, b: y + 5, c:3 };"
                 "x.a +' '+ x.b +' '+ x.c"),
      nxt_string("1 7 3") },

    { nxt_string("var x = { a: 1, b: { a:2, c:5 } };"
                 "x.a +' '+ x.b.a +' '+ x.b.c"),
      nxt_string("1 2 5") },

    { nxt_string("var y = 5, x = { a:y }; x.a"),
      nxt_string("5") },

    { nxt_string("var x = { a: 1; b: 2 }"),
      nxt_string("SyntaxError: Unexpected token \";\" in 1") },

    { nxt_string("var x = { a: 1, b: x.a }"),
      nxt_string("TypeError: cannot get property 'a' of undefined") },

    { nxt_string("var a = { b: 2 }; a.b += 1"),
      nxt_string("3") },

    { nxt_string("var o = {a:1}, c = o; o.a = o = {b:5};"
                 "o.a +' '+ o.b +' '+ c.a.b"),
      nxt_string("undefined 5 5") },

    { nxt_string("var y = { a: 2 }, x = { a: 1, b: y.a }; x.a +' '+ x.b"),
      nxt_string("1 2") },

    { nxt_string("var y = { a: 1 }, x = { a: y.a++, b: y.a++ }\n"
                 "x.a +' '+ x.b +' '+ y.a"),
      nxt_string("1 2 3") },

    { nxt_string("var a='', o = {a:1, b:2}, p;"
                 "for (p in o) { a += p +':'+ o[p] +',' } a"),
      nxt_string("a:1,b:2,") },

    { nxt_string("var x = { a: 1 }, b = delete x.a; x.a +' '+ b"),
      nxt_string("undefined true") },

    { nxt_string("delete null"),
      nxt_string("true") },

    { nxt_string("var a; delete a"),
      nxt_string("SyntaxError: Delete of an unqualified identifier in 1") },

    { nxt_string("delete undefined"),
      nxt_string("SyntaxError: Delete of an unqualified identifier in 1") },

    /* ES5FIX: "SyntaxError". */

    { nxt_string("delete NaN"),
      nxt_string("true") },

    /* ES5FIX: "SyntaxError". */

    { nxt_string("delete Infinity"),
      nxt_string("true") },

    { nxt_string("delete -Infinity"),
      nxt_string("true") },

    { nxt_string("delete (1/0)"),
      nxt_string("true") },

    { nxt_string("delete 1"),
      nxt_string("true") },

    { nxt_string("var a; delete (a = 1); a"),
      nxt_string("1") },

    { nxt_string("delete a"),
      nxt_string("SyntaxError: Delete of an unqualified identifier in 1") },

    { nxt_string("var a = 1; delete a"),
      nxt_string("SyntaxError: Delete of an unqualified identifier in 1") },

    { nxt_string("function f(){} delete f"),
      nxt_string("SyntaxError: Delete of an unqualified identifier in 1") },

    { nxt_string("var a = { x:1 }; ('x' in a) +' '+ (1 in a)"),
      nxt_string("true false") },

    { nxt_string("delete --[][1]"),
      nxt_string("true") },

    { nxt_string("var a = [1,2]; delete a.length"),
      nxt_string("false") },

    { nxt_string("var a = [1,2,3]; a.x = 10;  delete a[1]"),
      nxt_string("true") },

    { nxt_string("var o = Object.create({a:1}); o.a = 2; delete o.a; o.a"),
      nxt_string("1") },

    /* Inheritance. */

    { nxt_string("function Foo() {this.bar = 10;}; Foo.prototype.bar = 42; "
                 "var v = new Foo(); delete v.bar; v.bar"),
      nxt_string("42") },

    { nxt_string("function Cl(x,y) {this.x = x; this.y = y}; "
                 "var c = new Cl('a', 'b'); Cl.prototype.z = 1; c.z"),
      nxt_string("1") },

    /* Math object is immutable. */

    { nxt_string("delete Math.max"),
      nxt_string("TypeError: Cannot delete property 'max' of object") },

    { nxt_string("Math.E = 1"),
      nxt_string("TypeError: Cannot assign to read-only property 'E' of object") },

    { nxt_string("var o = { 'a': 1, 'b': 2 }; var i; "
                 "for (i in o) { delete o.a; delete o.b; }; njs.dump(o)"),
      nxt_string("{}") },

    { nxt_string("var o  = {}; Object.defineProperty(o, 'a', {value:1, configurable:1}); "
                 "delete o.a; o.a=2; o.a"),
      nxt_string("2") },

    { nxt_string("var a = {}; 1 in a"),
      nxt_string("false") },

    { nxt_string("'a' in {a:1}"),
      nxt_string("true") },

    { nxt_string("'a' in Object.create({a:1})"),
      nxt_string("true") },

    { nxt_string("var a = 1; 1 in a"),
      nxt_string("TypeError: property in on a primitive value") },

    { nxt_string("var a = true; 1 in a"),
      nxt_string("TypeError: property in on a primitive value") },

    { nxt_string("var n = { toString: function() { return 'a' } };"
                 "var o = { a: 5 }; o[n]"),
      nxt_string("5") },

    { nxt_string("var n = { valueOf: function() { return 'a' } };"
                 "var o = { a: 5, '[object Object]': 7 }; o[n]"),
      nxt_string("7") },

    { nxt_string("var o = {}; o.new = 'OK'; o.new"),
      nxt_string("OK") },

    { nxt_string("var o = { new: 'OK'}; o.new"),
      nxt_string("OK") },

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

    { nxt_string("var a = [ 1, 2, 3 ]; a[0] + a[1] + a[2]"),
      nxt_string("6") },

    { nxt_string("var a = [ 1, 2, 3 ]; a[-1] = 4; a + a[-1]"),
      nxt_string("1,2,34") },

    { nxt_string("var a = [ 1, 2, 3 ]; a[4294967295] = 4; a + a[4294967295]"),
      nxt_string("1,2,34") },

    { nxt_string("var a = [ 1, 2, 3 ]; a[4294967296] = 4; a + a[4294967296]"),
      nxt_string("1,2,34") },

    { nxt_string("delete[]['4e9']"),
      nxt_string("false") },

    { nxt_string("var n = 1, a = [ n += 1 ]; a"),
      nxt_string("2") },

    { nxt_string("var a = [ 1, 2; 3 ]; a[0] + a[1] + a[2]"),
      nxt_string("SyntaxError: Unexpected token \";\" in 1") },

    { nxt_string("var a = [ 1, 2, 3 ]; a[0] +' '+ a[1] +' '+ a[2] +' '+ a[3]"),
      nxt_string("1 2 3 undefined") },

    { nxt_string("var a = [ 5, 6, 7 ]; a['1']"),
      nxt_string("6") },

    { nxt_string("var a = [ 5, 6, 7 ]; a['01']"),
      nxt_string("undefined") },

    { nxt_string("var a = [ 5, 6, 7 ]; a[0x1]"),
      nxt_string("6") },

    { nxt_string("var a = [ 5, 6, 7 ]; a['0x1']"),
      nxt_string("undefined") },

    { nxt_string("[] - 2"),
      nxt_string("-2") },

    { nxt_string("[1] - 2"),
      nxt_string("-1") },

    { nxt_string("[[1]] - 2"),
      nxt_string("-1") },

    { nxt_string("[[[1]]] - 2"),
      nxt_string("-1") },

    { nxt_string("var a = []; a - 2"),
      nxt_string("-2") },

    { nxt_string("var a = [1]; a - 2"),
      nxt_string("-1") },

    { nxt_string("var a = []; a[0] = 1; a - 2"),
      nxt_string("-1") },

    { nxt_string("[] + 2 + 3"),
      nxt_string("23") },

    { nxt_string("[1] + 2 + 3"),
      nxt_string("123") },

    { nxt_string("var a = []; a + 2 + 3"),
      nxt_string("23") },

    { nxt_string("var a = [1]; a + 2 + 3"),
      nxt_string("123") },

    { nxt_string("var a = [1,2], i = 0; a[i++] += a[0] = 5 + i;"
                 "a[0] +' '+ a[1]"),
      nxt_string("7 2") },

    { nxt_string("var a = []; a[0] = 1; a + 2 + 3"),
      nxt_string("123") },

    { nxt_string("var a = []; a['0'] = 1; a + 2 + 3"),
      nxt_string("123") },

    { nxt_string("var a = []; a[2] = 1; a[2]"),
      nxt_string("1") },

    { nxt_string("var a = [1, 2]; 1 in a"),
      nxt_string("true") },

    { nxt_string("var a = [1, 2]; 2 in a"),
      nxt_string("false") },

    { nxt_string("var a = [1, 2]; delete a[0]; 0 in a"),
      nxt_string("false") },

    { nxt_string("var a = [ function(a) {return a + 1} ]; a[0](5)"),
      nxt_string("6") },

    { nxt_string("var s = '', a = [5,1,2], i;"
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

    { nxt_string("--[][3e9]"),
      nxt_string("MemoryError") },

    { nxt_string("[].length"),
      nxt_string("0") },

    { nxt_string("[1,2].length"),
      nxt_string("2") },

    { nxt_string("var a = [1,2]; a.length"),
      nxt_string("2") },

    { nxt_string("[\n1]"),
      nxt_string("1") },

    { nxt_string("\n[\n1\n]"),
      nxt_string("1") },

    { nxt_string("\n[\n1\n,\n2]\n[\n0]"),
      nxt_string("1") },

#if 0
    { nxt_string("Object.create([1,2]).length"),
      nxt_string("2") },
#endif

    { nxt_string("Object.create(['α','β'])[1]"),
      nxt_string("β") },

    { nxt_string("Object.create(['α','β'])[false]"),
      nxt_string("undefined") },

    /* Array.length setter */

    { nxt_string("[].length = {}"),
      nxt_string("RangeError: Invalid array length") },

    { nxt_string("[].length = 2**32 - 1"),
      nxt_string("MemoryError") },

    { nxt_string("[].length = 3e9"),
      nxt_string("MemoryError") },

    { nxt_string("Object.defineProperty([], 'length',{value: 2**32 - 1})"),
      nxt_string("MemoryError") },

    { nxt_string("[].length = 2**32"),
      nxt_string("RangeError: Invalid array length") },

    { nxt_string("[].length = 2**32 + 1"),
      nxt_string("RangeError: Invalid array length") },

    { nxt_string("[].length = -1"),
      nxt_string("RangeError: Invalid array length") },

    { nxt_string("var a = []; a.length = 0; JSON.stringify(a)"),
      nxt_string("[]") },

    { nxt_string("var a = []; a.length = 1; JSON.stringify(a)"),
      nxt_string("[null]") },

    { nxt_string("var a = [1]; a.length = 1; JSON.stringify(a)"),
      nxt_string("[1]") },

    { nxt_string("var a = [1]; a.length = 2; JSON.stringify(a)"),
      nxt_string("[1,null]") },

    { nxt_string("var a = [1]; a.length = 4; a.length = 0; JSON.stringify(a)"),
      nxt_string("[]") },

    { nxt_string("var a = [1,2,3]; a.length = 2; JSON.stringify(a)"),
      nxt_string("[1,2]") },

    { nxt_string("var a = [1,2,3]; a.length = 3; a"),
      nxt_string("1,2,3") },

    { nxt_string("var a = [1,2,3]; a.length = 16; a"),
      nxt_string("1,2,3,,,,,,,,,,,,,") },

    { nxt_string("var a = [1,2,3]; a.join()"),
      nxt_string("1,2,3") },

    { nxt_string("var a = [1,2,3]; a.join(':')"),
      nxt_string("1:2:3") },

    { nxt_string("var a = []; a[5] = 5; a.join()"),
      nxt_string(",,,,,5") },

    { nxt_string("var a = [,null,undefined,false,true,0,1]; a.join()"),
      nxt_string(",,,false,true,0,1") },

    { nxt_string("var o = { toString: function() { return null } };"
                 "[o].join()"),
      nxt_string("null") },

    { nxt_string("var o = { toString: function() { return undefined } };"
                 "[o].join()"),
      nxt_string("undefined") },

    { nxt_string("var a = []; a[5] = 5; a"),
      nxt_string(",,,,,5") },

    { nxt_string("var a = []; a.concat([])"),
      nxt_string("") },

    { nxt_string("var s = { toString: function() { return 'S' } };"
                 "var v = { toString: 8, valueOf: function() { return 'V' } };"
                 "var o = [9]; o.join = function() { return 'O' };"
                 "var a = [1,2,3,[4,5,6],s,v,o]; a.join('')"),
      nxt_string("1234,5,6SVO") },

    { nxt_string("var s = { toString: function() { return 'S' } };"
                 "var v = { toString: 8, valueOf: function() { return 'V' } };"
                 "var o = [9]; o.join = function() { return 'O' };"
                 "var a = [1,2,3,[4,5,6],s,v,o]; a"),
      nxt_string("1,2,3,4,5,6,S,V,O") },

    /* Array.toString(). */

# if 0
    { nxt_string("var a = [1,2,3]; a.join = 'NO';"
                 "Object.prototype.toString = function () { return 'A' }; a"),
      nxt_string("[object Array]") },
#endif

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

    { nxt_string("Array.isArray()"),
      nxt_string("false") },

    { nxt_string("Array.isArray(1)"),
      nxt_string("false") },

    { nxt_string("Array.isArray(1) ? 'true' : 'false'"),
      nxt_string("false") },

    { nxt_string("Array.isArray([])"),
      nxt_string("true") },

    { nxt_string("Array.isArray([]) ? 'true' : 'false'"),
      nxt_string("true") },

    { nxt_string("Array.of()"),
      nxt_string("") },

    { nxt_string("Array.of(1,2,3)"),
      nxt_string("1,2,3") },

    { nxt_string("Array.of(undefined,1)"),
      nxt_string(",1") },

    { nxt_string("Array.of(NaN,-1,{})"),
      nxt_string("NaN,-1,[object Object]") },

    { nxt_string("var a = [1,2,3]; a.concat(4, [5, 6, 7], 8)"),
      nxt_string("1,2,3,4,5,6,7,8") },

    { nxt_string("var a = []; a[100] = a.length; a[100] +' '+ a.length"),
      nxt_string("0 101") },

    { nxt_string("var a = [1,2]; a[100] = 100; a[100] +' '+ a.length"),
      nxt_string("100 101") },

    { nxt_string("Array.prototype.slice(1)"),
      nxt_string("") },

    { nxt_string("Array.prototype.slice(1,2)"),
      nxt_string("") },

    { nxt_string("Array.prototype.slice.call(undefined)"),
      nxt_string("TypeError: cannot convert undefined to object") },

    { nxt_string("Array.prototype.slice.call(1)"),
      nxt_string("") },

    { nxt_string("Array.prototype.slice.call(false)"),
      nxt_string("") },

    { nxt_string("Array.prototype.slice.call({'0':'a', '1':'b', length:1})"),
      nxt_string("a") },

    { nxt_string("Array.prototype.slice.call({'0':'a', '1':'b', length:2})"),
      nxt_string("a,b") },

    { nxt_string("Array.prototype.slice.call({'0':'a', '1':'b', length:4})"),
      nxt_string("a,b,,") },

    { nxt_string("Array.prototype.slice.call({'0':'a', '1':'b', length:2}, 1)"),
      nxt_string("b") },

    { nxt_string("Array.prototype.slice.call({'0':'a', '1':'b', length:2}, 1, 2)"),
      nxt_string("b") },

    { nxt_string("Array.prototype.slice.call({length:'2'})"),
      nxt_string(",") },

    { nxt_string("njs.dump(Array.prototype.slice.call({length: 3, 1: undefined }))"),
      nxt_string("[<empty>,undefined,<empty>]") },

    { nxt_string("Array.prototype.slice.call({length:new Number(3)})"),
      nxt_string(",,") },

    { nxt_string("Array.prototype.slice.call({length: { valueOf: function() { return 2; } }})"),
      nxt_string(",") },

    { nxt_string("Array.prototype.slice.call({ length: Object.create(null) })"),
      nxt_string("TypeError: Cannot convert object to primitive value") },

    { nxt_string("Array.prototype.slice.call({length:-1})"),
      nxt_string("MemoryError") },

    { nxt_string("Array.prototype.slice.call('αβZγ')"),
      nxt_string("α,β,Z,γ") },

    { nxt_string("Array.prototype.slice.call('αβZγ', 1)"),
      nxt_string("β,Z,γ") },

    { nxt_string("Array.prototype.slice.call('αβZγ', 2)"),
      nxt_string("Z,γ") },

    { nxt_string("Array.prototype.slice.call('αβZγ', 3)"),
      nxt_string("γ") },

    { nxt_string("Array.prototype.slice.call('αβZγ', 4)"),
      nxt_string("") },

    { nxt_string("Array.prototype.slice.call('αβZγ', 0, 1)"),
      nxt_string("α") },

    { nxt_string("Array.prototype.slice.call('αβZγ', 1, 2)"),
      nxt_string("β") },

    { nxt_string("Array.prototype.slice.call('αβZγ').length"),
      nxt_string("4") },

    { nxt_string("Array.prototype.slice.call('αβZγ')[1].length"),
      nxt_string("1") },

    { nxt_string("Array.prototype.slice.call(new String('αβZγ'))"),
      nxt_string("α,β,Z,γ") },

    { nxt_string("Array.prototype.pop()"),
      nxt_string("undefined") },

    { nxt_string("Array.prototype.shift()"),
      nxt_string("undefined") },

    { nxt_string("[0,1].slice()"),
      nxt_string("0,1") },

    { nxt_string("[0,1].slice(undefined)"),
      nxt_string("0,1") },

    { nxt_string("[0,1].slice(undefined, undefined)"),
      nxt_string("0,1") },

    { nxt_string("[0,1,2,3,4].slice(1,4)"),
      nxt_string("1,2,3") },

    { nxt_string("[0,1,2,3,4].slice(6,7)"),
      nxt_string("") },

    { nxt_string("var a = [1,2,3,4,5], b = a.slice(3);"
                 "b[0] +' '+ b[1] +' '+ b[2]"),
      nxt_string("4 5 undefined") },

    { nxt_string("var a = [1,2]; a.pop() +' '+ a.length +' '+ a"),
      nxt_string("2 1 1") },

    { nxt_string("var a = [1,2], len = a.push(3); len +' '+ a.pop() +' '+ a"),
      nxt_string("3 3 1,2") },

    { nxt_string("var a = [1,2], len = a.push(3,4,5);"
                 "len +' '+ a.pop() +' '+ a"),
      nxt_string("5 5 1,2,3,4") },

    { nxt_string("var a = [1,2,3]; a.shift() +' '+ a[0] +' '+ a.length"),
      nxt_string("1 2 2") },

    { nxt_string("var a = [1,2], len = a.unshift(3);"
                 "len +' '+ a +' '+ a.shift()"),
      nxt_string("3 3,1,2 3") },

    { nxt_string("var a = [1,2], len = a.unshift(3,4,5);"
                 "len +' '+ a +' '+ a.shift()"),
      nxt_string("5 3,4,5,1,2 3") },

    { nxt_string("var a = []; a.splice()"),
      nxt_string("") },

    { nxt_string("[].splice(0,5,0)"),
      nxt_string("") },

    { nxt_string("[1,2,3,4,5].splice(-2,3,0)"),
      nxt_string("4,5") },

    { nxt_string("[].__proto__.splice(0,1,0)"),
      nxt_string("") },

    { nxt_string("var a = [];"
                 "a.splice(9,0,1,2).join(':') + '|' + a"),
      nxt_string("|1,2") },

    { nxt_string("var a = [0,1,2,3,4,5,6,7];"
                 "a.splice(3).join(':') + '|' + a"),
      nxt_string("3:4:5:6:7|0,1,2") },

    { nxt_string("var a = [0,1,2,3,4,5,6,7];"
                 "a.splice(3, 2).join(':') + '|' + a"),
      nxt_string("3:4|0,1,2,5,6,7") },

    { nxt_string("var a = [0,1,2,3,4,5,6,7];"
                 "a.splice(3, 2, 8, 9, 10, 11 ).join(':') + '|' + a"),
      nxt_string("3:4|0,1,2,8,9,10,11,5,6,7") },

    { nxt_string("var a = []; a.reverse()"),
      nxt_string("") },

    { nxt_string("var a = [1]; a.reverse()"),
      nxt_string("1") },

    { nxt_string("var a = [1,2]; a.reverse()"),
      nxt_string("2,1") },

    { nxt_string("var a = [1,2,3]; a.reverse()"),
      nxt_string("3,2,1") },

    { nxt_string("var a = [1,2,3,4]; a.reverse()"),
      nxt_string("4,3,2,1") },

    { nxt_string("var a = [1,2,3,4]; a.indexOf()"),
      nxt_string("-1") },

    { nxt_string("var a = [1,2,3,4]; a.indexOf(5)"),
      nxt_string("-1") },

    { nxt_string("var a = [1,2,3,4]; a.indexOf(4, 3)"),
      nxt_string("3") },

    { nxt_string("var a = [1,2,3,4]; a.indexOf(4, 4)"),
      nxt_string("-1") },

    { nxt_string("var a = [1,2,3,4,3,4]; a.indexOf(3, '2')"),
      nxt_string("2") },

    { nxt_string("var a = [1,2,3,4,3,4]; a.indexOf(4, -1)"),
      nxt_string("5") },

    { nxt_string("var a = [1,2,3,4,3,4]; a.indexOf(3, -10)"),
      nxt_string("2") },

    { nxt_string("[].indexOf.bind(0)(0, 0)"),
      nxt_string("-1") },

    { nxt_string("[].lastIndexOf(1, -1)"),
      nxt_string("-1") },

    { nxt_string("var a = [1,2,3,4]; a.lastIndexOf()"),
      nxt_string("-1") },

    { nxt_string("var a = [1,2,3,4]; a.lastIndexOf(5)"),
      nxt_string("-1") },

    { nxt_string("var a = [1,2,3,4,3,4]; a.lastIndexOf(1, 0)"),
      nxt_string("0") },

    { nxt_string("var a = [1,2,3,4,3,4]; a.lastIndexOf(3, '2')"),
      nxt_string("2") },

    { nxt_string("var a = [1,2,3,4,3,4]; a.lastIndexOf(1, 6)"),
      nxt_string("0") },

    { nxt_string("var a = [1,2,3,4,3,4]; a.lastIndexOf(2, 6)"),
      nxt_string("1") },

    { nxt_string("var a = [1,2,3,4,3,4]; a.lastIndexOf(4, -1)"),
      nxt_string("5") },

    { nxt_string("var a = [1,2,3,4,3,4]; a.lastIndexOf(4, -6)"),
      nxt_string("-1") },

    { nxt_string("var a = [1,2,3,4,3,4]; a.lastIndexOf(3, -10)"),
      nxt_string("-1") },

    { nxt_string("[1,2,3,4].includes()"),
      nxt_string("false") },

    { nxt_string("[1,2,3,4].includes(5)"),
      nxt_string("false") },

    { nxt_string("[1,2,3,4].includes(4, 3)"),
      nxt_string("true") },

    { nxt_string("[1,2,3,4].includes(4, 4)"),
      nxt_string("false") },

    { nxt_string("[1,2,3,4,3,4].includes(3, '2')"),
      nxt_string("true") },

    { nxt_string("[1,2,3,4,3,4].includes(4, -1)"),
      nxt_string("true") },

    { nxt_string("[1,2,3,4,3,4].includes(3, -10)"),
      nxt_string("true") },

    { nxt_string("[1,2,3,NaN,3,4].includes(NaN)"),
      nxt_string("true") },

    { nxt_string("[1,2,3,4,5].includes(NaN)"),
      nxt_string("false") },

    { nxt_string("[].includes.bind(0)(0, 0)"),
      nxt_string("false") },

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

    { nxt_string("function f() { var c; [1].forEach(function(v) { c })}; f()"),
      nxt_string("undefined") },

    { nxt_string("var a = [1,2,3]; var s = { sum: 0 };"
                 "[].forEach.call(a, function(v, i, a) { this.sum += v }, s);"
                 "s.sum"),
      nxt_string("6") },

    { nxt_string("var a = [1,2,3]; var s = { sum: 0 };"
                 "[].forEach.apply(a,"
                                  "[ function(v, i, a) { this.sum += v }, s ]);"
                 "s.sum"),
      nxt_string("6") },

    { nxt_string("var a = []; var c = 0;"
                 "a.forEach(function(v, i, a) { c++ }); c"),
      nxt_string("0") },

    { nxt_string("var a = [,,,,]; var c = 0;"
                 "a.forEach(function(v, i, a) { c++ }); c"),
      nxt_string("0") },

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

    { nxt_string("[].fill(1);"),
      nxt_string("") },

    { nxt_string("[1,2,3].fill(5);"),
      nxt_string("5,5,5") },

    { nxt_string("[1,2,3].fill(5, 0);"),
      nxt_string("5,5,5") },

    { nxt_string("[1,2,3].fill(5, 1);"),
      nxt_string("1,5,5") },

    { nxt_string("[1,2,3].fill(5, 4);"),
      nxt_string("1,2,3") },

    { nxt_string("[1,2,3].fill(5, -2);"),
      nxt_string("1,5,5") },

    { nxt_string("[1,2,3].fill(5, -3);"),
      nxt_string("5,5,5") },

    { nxt_string("[1,2,3].fill(5, -4);"),
      nxt_string("5,5,5") },

    { nxt_string("[1,2,3].fill(5, 1, 0);"),
      nxt_string("1,2,3") },

    { nxt_string("[1,2,3].fill(5, 1, 1);"),
      nxt_string("1,2,3") },

    { nxt_string("[1,2,3].fill(5, 1, 2);"),
      nxt_string("1,5,3") },

    { nxt_string("[1,2,3].fill(5, 1, 3);"),
      nxt_string("1,5,5") },

    { nxt_string("[1,2,3].fill(5, 1, 4);"),
      nxt_string("1,5,5") },

    { nxt_string("[1,2,3].fill(5, 1, -1);"),
      nxt_string("1,5,3") },

    { nxt_string("[1,2,3].fill(5, 1, -3);"),
      nxt_string("1,2,3") },

    { nxt_string("[1,2,3].fill(5, 1, -4);"),
      nxt_string("1,2,3") },

    { nxt_string("[1,2,3].fill(\"a\", 1, 2);"),
      nxt_string("1,a,3") },

    { nxt_string("[1,2,3].fill({a:\"b\"}, 1, 2);"),
      nxt_string("1,[object Object],3") },

    { nxt_string("var a = [];"
                 "a.filter(function(v, i, a) { return v > 1 })"),
      nxt_string("") },

    { nxt_string("var a = [1,2,3,-1,5];"
                 "a.filter(function(v, i, a) { return v > 1 })"),
      nxt_string("2,3,5") },

    { nxt_string("var a = [1,2,3,4,5,6,7,8];"
                 "a.filter(function(v, i, a) { a.pop(); return v > 1 })"),
      nxt_string("2,3,4") },

    { nxt_string("var a = [1,2,3,4,5,6,7,8];"
                 "a.filter(function(v, i, a) { a.shift(); return v > 1 })"),
      nxt_string("3,5,7") },

    { nxt_string("var a = [1,2,3,4,5,6,7];"
                 "a.filter(function(v, i, a) { a.pop(); return v > 1 })"),
      nxt_string("2,3,4") },

    { nxt_string("var a = [1,2,3,4,5,6,7];"
                 "a.filter(function(v, i, a) { a.shift(); return v > 1 })"),
      nxt_string("3,5,7") },

    { nxt_string("var a = [1,2,3,4,5,6,7];"
                 "a.filter(function(v, i, a) { a[i] = v + 1; return true })"),
      nxt_string("1,2,3,4,5,6,7") },

    { nxt_string("var a = [1,2,3,4,5,6,7];"
                 "a.filter(function(v, i, a) { a[i+1] = v+10; return true })"),
      nxt_string("1,11,21,31,41,51,61") },

    { nxt_string("var a = [];"
                 "a.find(function(v, i, a) { return v > 1 })"),
      nxt_string("undefined") },

    { nxt_string("var a = [,NaN,0,-1];"
                 "a.find(function(v, i, a) { return v > 1 })"),
      nxt_string("undefined") },

    { nxt_string("var a = [,NaN,0,-1,2];"
                 "a.find(function(v, i, a) { return v > 1 })"),
      nxt_string("2") },

    { nxt_string("var a = [1,2,3,-1,5];"
                 "a.find(function(v, i, a) { return v > 1 })"),
      nxt_string("2") },

    { nxt_string("var a = [,1,,-1,5];"
                 "a.find(function(v, i, a) { return v > 1 })"),
      nxt_string("5") },

    { nxt_string("var a = [,1,,-1,5,6];"
                 "a.find(function(v, i, a) { return v > 1 })"),
      nxt_string("5") },

    { nxt_string("[].find(function(v) { return (v === undefined) })"),
      nxt_string("undefined") },

    { nxt_string("var a = [,3];"
                 "a.find(function(v) { return (v === 3 || v === undefined) })"),
      nxt_string("undefined") },

    { nxt_string("var a = [1,,3];"
                 "a.find(function(v) { return (v === 3 || v === undefined) })"),
      nxt_string("undefined") },

    { nxt_string("var a = [1,2,3,4,5,6];"
                 "a.find(function(v, i, a) { a.shift(); return v == 3 })"),
      nxt_string("3") },

    { nxt_string("var a = [1,2,3,4,5,6];"
                 "a.find(function(v, i, a) { a.shift(); return v == 4 })"),
      nxt_string("undefined") },

    { nxt_string("var a = [];"
                 "a.findIndex(function(v, i, a) { return v > 1 })"),
      nxt_string("-1") },

    { nxt_string("var a = [,NaN,0,-1];"
                 "a.findIndex(function(v, i, a) { return v > 1 })"),
      nxt_string("-1") },

    { nxt_string("var a = [,NaN,0,-1,2];"
                 "a.findIndex(function(v, i, a) { return v > 1 })"),
     nxt_string("4") },

    { nxt_string("var a = [1,2,3,-1,5];"
                 "a.findIndex(function(v, i, a) { return v > 1 })"),
      nxt_string("1") },

    { nxt_string("var a = [,1,,-1,5];"
                 "a.findIndex(function(v, i, a) { return v > 1 })"),
      nxt_string("4") },

    { nxt_string("var a = [,1,,-1,5,6];"
                 "a.findIndex(function(v, i, a) { return v > 1 })"),
      nxt_string("4") },

    { nxt_string("[].findIndex(function(v) { return (v === undefined) })"),
      nxt_string("-1") },

    { nxt_string("[,].findIndex(function(v) { return (v === undefined) })"),
      nxt_string("0") },

    { nxt_string("[1,2,,3].findIndex(function(el){return el === undefined})"),
      nxt_string("2") },

    { nxt_string("[,2,,3].findIndex(function(el){return el === undefined})"),
      nxt_string("0") },

    { nxt_string("var a = [1,2,3,4,5,6];"
                 "a.findIndex(function(v, i, a) { a.shift(); return v == 3 })"),
      nxt_string("1") },

    { nxt_string("var a = [1,2,3,4,5,6];"
                 "a.findIndex(function(v, i, a) { a.shift(); return v == 4 })"),
      nxt_string("-1") },

    { nxt_string("var a = [];"
                 "a.map(function(v, i, a) { return v + 1 })"),
      nxt_string("") },

    { nxt_string("var a = [,,,];"
                 "a.map(function(v, i, a) { return v + 1 })"),
      nxt_string(",,") },

    { nxt_string("var a = [,,,1];"
                 "a.map(function(v, i, a) { return v + 1 })"),
      nxt_string(",,,2") },

    { nxt_string("var a = [1,2,3];"
                 "a.map(function(v, i, a) { return v + 1 })"),
      nxt_string("2,3,4") },

    { nxt_string("var a = [1,2,3,4,5,6];"
                 "a.map(function(v, i, a) { a.pop(); return v + 1 })"),
      nxt_string("2,3,4,,,") },

    { nxt_string("var a = [1,2,3,4,5,6];"
                 "a.map(function(v, i, a) { a.shift(); return v + 1 })"),
      nxt_string("2,4,6,,,") },

    { nxt_string("var a = [];"
                 "a.reduce(function(p, v, i, a) { return p + v })"),
      nxt_string("TypeError: invalid index") },

    { nxt_string("var a = [];"
                 "a.reduce(function(p, v, i, a) { return p + v }, 10)"),
      nxt_string("10") },

    { nxt_string("var a = [,,];"
                 "a.reduce(function(p, v, i, a) { return p + v })"),
      nxt_string("TypeError: invalid index") },

    { nxt_string("var a = [,,];"
                 "a.reduce(function(p, v, i, a) { return p + v }, 10)"),
      nxt_string("10") },

    { nxt_string("var a = [1];"
                 "a.reduce(function(p, v, i, a) { return p + v })"),
      nxt_string("1") },

    { nxt_string("var a = [1];"
                 "a.reduce(function(p, v, i, a) { return p + v }, 10)"),
      nxt_string("11") },

    { nxt_string("var a = [1,2,3];"
                 "a.reduce(function(p, v, i, a) { return p + v })"),
      nxt_string("6") },

    { nxt_string("var a = [1,2,3];"
                 "a.reduce(function(p, v, i, a) { return p + v }, 10)"),
      nxt_string("16") },

    { nxt_string("[[0, 1], [2, 3], [4, 5]].reduce(function(a, b)"
                 "                         { return a.concat(b) }, [])"),
      nxt_string("0,1,2,3,4,5") },

    { nxt_string("var a = [];"
                 "a.reduceRight(function(p, v, i, a) { return p + v })"),
      nxt_string("TypeError: invalid index") },

    { nxt_string("var a = [];"
                 "a.reduceRight(function(p, v, i, a) { return p + v }, 10)"),
      nxt_string("10") },

    { nxt_string("var a = [,,];"
                 "a.reduceRight(function(p, v, i, a) { return p + v })"),
      nxt_string("TypeError: invalid index") },

    { nxt_string("var a = [,,];"
                 "a.reduceRight(function(p, v, i, a) { return p + v }, 10)"),
      nxt_string("10") },

    { nxt_string("var a = [1];"
                 "a.reduceRight(function(p, v, i, a) { return p + v })"),
      nxt_string("1") },

    { nxt_string("var a = [1];"
                 "a.reduceRight(function(p, v, i, a) { return p + v }, 10)"),
      nxt_string("11") },

    { nxt_string("var a = [1,2,3];"
                 "a.reduceRight(function(p, v, i, a) { return p + v })"),
      nxt_string("6") },

    { nxt_string("var a = [1,2,3];"
                 "a.reduceRight(function(p, v, i, a) { return p + v }, 10)"),
      nxt_string("16") },

    { nxt_string("var a = [1,2,3];"
                 "a.reduceRight(function(p, v, i, a)"
                 "              { a.shift(); return p + v })"),
      nxt_string("7") },

    { nxt_string("var a = [1,2,3];"
                 "a.reduceRight(function(p, v, i, a)"
                 "              { a.shift(); return p + v }, 10)"),
      nxt_string("19") },

    { nxt_string("var a = ['1','2','3','4','5','6']; a.sort()"),
      nxt_string("1,2,3,4,5,6") },

    { nxt_string("var o = { toString: function() { return 5 } };"
                 "var a = [6,o,4,3,2,1]; a.sort()"),
      nxt_string("1,2,3,4,5,6") },

    { nxt_string("var a = [1,2,3,4,5,6];"
                 "a.sort(function(x, y) { return x - y })"),
      nxt_string("1,2,3,4,5,6") },

    { nxt_string("var a = [6,5,4,3,2,1];"
                 "a.sort(function(x, y) { return x - y })"),
      nxt_string("1,2,3,4,5,6") },

    { nxt_string("var a = [2,2,2,1,1,1];"
                 "a.sort(function(x, y) { return x - y })"),
      nxt_string("1,1,1,2,2,2") },

    { nxt_string("var a = [,,,2,2,2,1,1,1];"
                 "a.sort(function(x, y) { return x - y })"),
      nxt_string("1,1,1,2,2,2,,,") },

    { nxt_string("var a = [,,,,];"
                 "a.sort(function(x, y) { return x - y })"),
      nxt_string(",,,") },

    { nxt_string("var a = [1,,];"
                 "a.sort(function(x, y) { return x - y })"),
      nxt_string("1,") },

    /* Strings. */

    { nxt_string("var a = '0123456789' + '012345';"
                 "var b = 'abcdefghij' + 'klmnop';"
                 "    a = b"),
      nxt_string("abcdefghijklmnop") },

    { nxt_string("String.prototype.my = function f() {return 7}; 'a'.my()"),
      nxt_string("7") },

    { nxt_string("'a'.my"),
      nxt_string("undefined") },

    { nxt_string("var a = '123'\n[2].toString();a"),
      nxt_string("3") },

    /* Escape strings. */

    { nxt_string("'\\a \\' \\\" \\\\ \\0 \\b \\f \\n \\r \\t \\v'"),
      nxt_string("a ' \" \\ \0 \b \f \n \r \t \v") },

    { nxt_string("'a\\\nb'"),
      nxt_string("ab") },

    { nxt_string("'a\\\rb'"),
      nxt_string("ab") },

    { nxt_string("'a\\\r\nb'"),
      nxt_string("ab") },

    { nxt_string("'abcde"),
      nxt_string("SyntaxError: Unterminated string \"'abcde\" in 1") },

    { nxt_string("'\\"),
      nxt_string("SyntaxError: Unterminated string \"'\\\" in 1") },

    { nxt_string("'\\'"),
      nxt_string("SyntaxError: Unterminated string \"'\\'\" in 1") },

    { nxt_string("'\\u03B1'"),
      nxt_string("α") },

    { nxt_string("'\\u'"),
      nxt_string("SyntaxError: Invalid Unicode code point \"\\u\" in 1") },

    { nxt_string("'\\uzzzz'"),
      nxt_string("SyntaxError: Invalid Unicode code point \"\\uzzzz\" in 1") },

    { nxt_string("'\\u03B'"),
      nxt_string("SyntaxError: Invalid Unicode code point \"\\u03B\" in 1") },

    { nxt_string("'\\u03BG'"),
      nxt_string("SyntaxError: Invalid Unicode code point \"\\u03BG\" in 1") },

    { nxt_string("'\\u03B '"),
      nxt_string("SyntaxError: Invalid Unicode code point \"\\u03B \" in 1") },

    { nxt_string("'\\u{61}\\u{3B1}\\u{20AC}'"),
      nxt_string("aα€") },

    { nxt_string("'\\u'"),
      nxt_string("SyntaxError: Invalid Unicode code point \"\\u\" in 1") },

    { nxt_string("'\\u{'"),
      nxt_string("SyntaxError: Invalid Unicode code point \"\\u{\" in 1") },

    { nxt_string("'\\u{}'"),
      nxt_string("SyntaxError: Invalid Unicode code point \"\\u{}\" in 1") },

    { nxt_string("'\\u{1234567}'"),
      nxt_string("SyntaxError: Invalid Unicode code point \"\\u{1234567}\" in 1") },

    { nxt_string("'\\u{a00000}'"),
      nxt_string("SyntaxError: Invalid Unicode code point \"\\u{a00000}\" in 1") },

    { nxt_string("'\\x61'"),
      nxt_string("a") },

    { nxt_string("''.length"),
      nxt_string("0") },

    { nxt_string("'abc'.length"),
      nxt_string("3") },

    { nxt_string("''.hasOwnProperty('length')"),
      nxt_string("true") },

    { nxt_string("'abc'.hasOwnProperty('length')"),
      nxt_string("true") },

    { nxt_string("(new String('abc')).hasOwnProperty('length')"),
      nxt_string("true") },

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

    { nxt_string("var a = 'abc'; a.length"),
      nxt_string("3") },

    { nxt_string("var a = 'abc'; a['length']"),
      nxt_string("3") },

    { nxt_string("var a = 'абв'; a.length"),
      nxt_string("3") },

    { nxt_string("var a = 'abc' + 'абв'; a.length"),
      nxt_string("6") },

    { nxt_string("var a = 'abc' + 1 + 'абв'; a +' '+ a.length"),
      nxt_string("abc1абв 7") },

    { nxt_string("var a = 1; a.length"),
      nxt_string("undefined") },

    { nxt_string("var a = 'abc'; a.concat('абв', 123)"),
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

    { nxt_string("var b = '\\u00C2\\u00B6'.toBytes(), u = b.fromUTF8();"
                 "b.length +' '+ b +' '+ u.length +' '+ u"),
      nxt_string("2 ¶ 1 ¶") },

    { nxt_string("'α'.toBytes()"),
      nxt_string("null") },

    { nxt_string("'α'.toUTF8()[0]"),
      nxt_string("\xCE") },

    { nxt_string("/^\\x80$/.test('\\x80'.toBytes())"),
      nxt_string("true") },

    { nxt_string("/^\\xC2\\x80$/.test('\\x80'.toUTF8())"),
      nxt_string("true") },

    { nxt_string("'α'.toUTF8().toBytes()"),
      nxt_string("α") },

    { nxt_string("var a = 'a'.toBytes() + 'α'; a + a.length"),
      nxt_string("aα3") },

    { nxt_string("var a = 'µ§±®'.toBytes(); a"),
      nxt_string("\xB5\xA7\xB1\xAE") },

    { nxt_string("var a = 'µ§±®'.toBytes(2); a"),
      nxt_string("\xB1\xAE") },

    { nxt_string("var a = 'µ§±®'.toBytes(1,3); a"),
      nxt_string("\xA7\xB1") },

    { nxt_string("var a = '\\xB5\\xA7\\xB1\\xAE'.toBytes(); a.fromBytes()"),
      nxt_string("µ§±®") },

    { nxt_string("var a = '\\xB5\\xA7\\xB1\\xAE'.toBytes(); a.fromBytes(2)"),
      nxt_string("±®") },

    { nxt_string("var a = '\\xB5\\xA7\\xB1\\xAE'.toBytes(); a.fromBytes(1, 3)"),
      nxt_string("§±") },

    { nxt_string("var a = 'abcdefgh'; a.substr(3, 15)"),
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

    { nxt_string("'abcdefgh'.slice(undefined, undefined)"),
      nxt_string("abcdefgh") },

    { nxt_string("'abcdefgh'.slice(undefined)"),
      nxt_string("abcdefgh") },

    { nxt_string("'abcdefgh'.slice(undefined, 1)"),
      nxt_string("a") },

    { nxt_string("'abcdefgh'.slice(3, undefined)"),
      nxt_string("defgh") },

    { nxt_string("'abcde'.slice(50)"),
      nxt_string("") },

    { nxt_string("'abcde'.slice(1, 50)"),
      nxt_string("bcde") },

    { nxt_string("'abcdefgh'.slice(3, -2)"),
      nxt_string("def") },

    { nxt_string("'abcdefgh'.slice(5, 3)"),
      nxt_string("") },

    { nxt_string("'abcdefgh'.slice(100, 120)"),
      nxt_string("") },

    { nxt_string("String.prototype.substring(1, 5)"),
      nxt_string("") },

    { nxt_string("String.prototype.slice(1, 5)"),
      nxt_string("") },

    { nxt_string("String.prototype.toBytes(1, 5)"),
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

    { nxt_string("var o = { valueOf: function() {return 2} };"
                 "'abc'.charAt(o)"),
      nxt_string("c") },

    { nxt_string("var o = { toString: function() {return '2'} };"
                 "'abc'.charAt(o)"),
      nxt_string("c") },

    { nxt_string("'abc'.charCodeAt(1 + 1)"),
      nxt_string("99") },

    { nxt_string("'abc'.charCodeAt(3)"),
      nxt_string("NaN") },

    { nxt_string("var a = 'abcdef'; a.3"),
      nxt_string("SyntaxError: Unexpected token \".3\" in 1") },

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

    { nxt_string("'abcdef'['1']"),
      nxt_string("b") },

    { nxt_string("'abcdef'[' 1']"),
      nxt_string("undefined") },

    { nxt_string("'abcdef'['1 ']"),
      nxt_string("undefined") },

    { nxt_string("'abcdef'['']"),
      nxt_string("undefined") },

    { nxt_string("'abcdef'['-']"),
      nxt_string("undefined") },

    { nxt_string("'abcdef'['-1']"),
      nxt_string("undefined") },

    { nxt_string("'abcdef'['01']"),
      nxt_string("undefined") },

    { nxt_string("'abcdef'['0x01']"),
      nxt_string("undefined") },

    { nxt_string("var a = 'abcdef', b = 1 + 2; a[b]"),
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
      nxt_string("TypeError: unexpected value type:number") },

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

    { nxt_string("var a = $r.uri, s = a.fromUTF8(); s.length +' '+ s"),
      nxt_string("3 АБВ") },

    { nxt_string("var a = $r.uri, b = $r2.uri, c = $r3.uri; a+b+c"),
      nxt_string("АБВαβγabc") },

    { nxt_string("var a = $r.uri; $r.uri = $r2.uri; $r2.uri = a; $r2.uri+$r.uri"),
      nxt_string("АБВαβγ") },

    { nxt_string("var a = $r.uri, s = a.fromUTF8(2); s.length +' '+ s"),
      nxt_string("2 БВ") },

    { nxt_string("var a = $r.uri, s = a.fromUTF8(2, 4); s.length +' '+ s"),
      nxt_string("1 Б") },

    { nxt_string("var a = $r.uri; a +' '+ a.length +' '+ a"),
      nxt_string("АБВ 6 АБВ") },

    { nxt_string("$r.uri = 'αβγ'; var a = $r.uri; a.length +' '+ a"),
      nxt_string("6 αβγ") },

    { nxt_string("$r.uri.length +' '+ $r.uri"),
      nxt_string("6 АБВ") },

    { nxt_string("$r.uri = $r.uri.substr(2); $r.uri.length +' '+ $r.uri"),
      nxt_string("4 БВ") },

    { nxt_string("'' + $r.props.a + $r2.props.a + $r.props.a"),
      nxt_string("121") },

    { nxt_string("var p1 = $r.props, p2 = $r2.props; '' + p2.a + p1.a"),
      nxt_string("21") },

    { nxt_string("var p1 = $r.props, p2 = $r2.props; '' + p1.a + p2.a"),
      nxt_string("12") },

    { nxt_string("var p = $r3.props; p.a = 1"),
      nxt_string("TypeError: Cannot assign to read-only property 'a' of external") },
    { nxt_string("var p = $r3.props; delete p.a"),
      nxt_string("TypeError: Cannot delete property 'a' of external") },

    { nxt_string("$r.vars.p + $r2.vars.q + $r3.vars.k"),
      nxt_string("pvalqvalkval") },

    { nxt_string("$r.vars.unset"),
      nxt_string("undefined") },

    { nxt_string("var v = $r3.vars; v.k"),
      nxt_string("kval") },

    { nxt_string("var v = $r3.vars; v.unset = 1; v.unset"),
      nxt_string("1") },

    { nxt_string("$r.vars.unset = 'a'; $r2.vars.unset = 'b';"
                 "$r.vars.unset + $r2.vars.unset"),
      nxt_string("ab") },

    { nxt_string("$r.vars.unset = 1; $r2.vars.unset = 2;"
                 "$r.vars.unset + $r2.vars.unset"),
      nxt_string("12") },

    { nxt_string("$r3.vars.p = 'a'; $r3.vars.p2 = 'b';"
                 "$r3.vars.p + $r3.vars.p2"),
      nxt_string("ab") },

    { nxt_string("$r3.vars.p = 'a'; delete $r3.vars.p; $r3.vars.p"),
      nxt_string("undefined") },

    { nxt_string("$r3.vars.p = 'a'; delete $r3.vars.p; $r3.vars.p = 'b'; $r3.vars.p"),
      nxt_string("b") },

    { nxt_string("$r3.vars.error = 1"),
      nxt_string("Error: cannot set 'error' prop") },

    { nxt_string("delete $r3.vars.error"),
      nxt_string("Error: cannot delete 'error' prop") },

    { nxt_string("delete $r3.vars.e"),
      nxt_string("true") },

    { nxt_string("$r3.consts.k"),
      nxt_string("kval") },

    { nxt_string("$r3.consts.k = 1"),
      nxt_string("TypeError: Cannot assign to read-only property 'k' of external") },

    { nxt_string("delete $r3.consts.k"),
      nxt_string("TypeError: Cannot delete property 'k' of external") },

    { nxt_string("delete $r3.vars.p; $r3.vars.p"),
      nxt_string("undefined") },

    { nxt_string("var a = $r.host; a +' '+ a.length +' '+ a"),
      nxt_string("АБВГДЕЁЖЗИЙ 22 АБВГДЕЁЖЗИЙ") },

    { nxt_string("var a = $r.host; a.substr(2, 2)"),
      nxt_string("Б") },

    { nxt_string("var a = $r.header['User-Agent']; a +' '+ a.length +' '+ a"),
      nxt_string("User-Agent|АБВ 17 User-Agent|АБВ") },

    { nxt_string("var a='', p;"
                 "for (p in $r.header) { a += p +':'+ $r.header[p] +',' }"
                 "a"),
      nxt_string("01:01|АБВ,02:02|АБВ,03:03|АБВ,") },

    { nxt_string("$r.some_method('YES')"),
      nxt_string("АБВ") },

    { nxt_string("$r.create('XXX').uri"),
      nxt_string("XXX") },

    { nxt_string("var sr = $r.create('XXX'); sr.uri = 'YYY'; sr.uri"),
      nxt_string("YYY") },

    { nxt_string("var sr = $r.create('XXX'), sr2 = $r.create('YYY');"
                 "sr.uri = 'ZZZ'; "
                 "sr.uri + sr2.uri"),
      nxt_string("ZZZYYY") },

    { nxt_string("var sr = $r.create('XXX'); sr.vars.p = 'a'; sr.vars.p"),
      nxt_string("a") },

    { nxt_string("var p; for (p in $r.some_method);"),
      nxt_string("undefined") },

    { nxt_string("'uri' in $r"),
      nxt_string("true") },

    { nxt_string("'one' in $r"),
      nxt_string("false") },

    { nxt_string("'a' in $r.props"),
      nxt_string("true") },

    { nxt_string("delete $r.uri"),
      nxt_string("TypeError: Cannot delete property 'uri' of external") },

    { nxt_string("delete $r.one"),
      nxt_string("TypeError: Cannot delete property 'one' of external") },

    { nxt_string("$r.some_method.call($r, 'YES')"),
      nxt_string("АБВ") },

    { nxt_string("var f = $r.some_method.bind($r); f('YES')"),
      nxt_string("АБВ") },

    { nxt_string("function f(fn, arg) {return fn(arg);}; f($r.some_method.bind($r), 'YES')"),
      nxt_string("АБВ") },

    { nxt_string("$r.some_method.apply($r, ['YES'])"),
      nxt_string("АБВ") },

    { nxt_string("$r.some_method.call([], 'YES')"),
      nxt_string("TypeError: external value is expected") },

    { nxt_string("$r.nonexistent"),
      nxt_string("undefined") },

    { nxt_string("$r.error = 'OK'"),
      nxt_string("TypeError: Cannot assign to read-only property 'error' of external") },

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

    { nxt_string("var o = {b:$r.props.b}; o.b"),
      nxt_string("42") },

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

    { nxt_string("var a = 'abcdef'.substr(2, 4).charAt(2).length; a"),
      nxt_string("1") },

    { nxt_string("var a = 'abcdef'.substr(2, 4).charAt(2) + '1234'; a"),
      nxt_string("e1234") },

    { nxt_string("var a = ('abcdef'.substr(2, 5 * 2 - 6).charAt(2) + '1234')"
                 "         .length; a"),
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
                 "    var n;"
                 "    for (n = 0; n <= 1114111; n++) {"
                 "        if (String.fromCharCode(n).charCodeAt(0) !== n)"
                 "            return n;"
                 "    }"
                 "    return -1"
                 "})()"),
      nxt_string("-1") },

    { nxt_string("var a = 'abcdef'; function f(a) {"
                 "return a.slice(a.indexOf('cd')) } f(a)"),
      nxt_string("cdef") },

    { nxt_string("var a = 'abcdef'; a.slice(a.indexOf('cd'))"),
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

    { nxt_string("''.indexOf('')"),
      nxt_string("0") },

    { nxt_string("'12345'.indexOf(45, '0')"),
      nxt_string("3") },

    { nxt_string("'12'.indexOf('12345')"),
      nxt_string("-1") },

    { nxt_string("''.indexOf.call(12345, 45, '0')"),
      nxt_string("3") },

    { nxt_string("'abc'.lastIndexOf('abcdef')"),
      nxt_string("-1") },

    { nxt_string("'abc abc abc abc'.lastIndexOf('abc')"),
      nxt_string("12") },

    { nxt_string("'abc abc abc abc'.lastIndexOf('abc', 11)"),
      nxt_string("8") },

    { nxt_string("'abc abc abc abc'.lastIndexOf('abc', 0)"),
      nxt_string("0") },

    { nxt_string("'abc abc abc abc'.lastIndexOf('', 0)"),
      nxt_string("0") },

    { nxt_string("'abc abc abc abc'.lastIndexOf('', 5)"),
      nxt_string("5") },

    { nxt_string("'abc abc абвгд abc'.lastIndexOf('абвгд')"),
      nxt_string("8") },

    { nxt_string("'abc abc абвгдежз'.lastIndexOf('абвгд')"),
      nxt_string("8") },

    { nxt_string("'abc abc абвгдежз'.lastIndexOf('абвгд', 11)"),
      nxt_string("8") },

    { nxt_string("'abc abc абвгдежз'.lastIndexOf('абвгд', 12)"),
      nxt_string("8") },

    { nxt_string("'abc abc абвгдежз'.lastIndexOf('абвгд', 13)"),
      nxt_string("8") },

    { nxt_string("'abc abc абвгдежз'.lastIndexOf('абвгд', 14)"),
      nxt_string("8") },

    { nxt_string("'abc abc абвгдежз'.lastIndexOf('абвгд', 15)"),
      nxt_string("8") },

    { nxt_string("'abc abc абвгдежз'.lastIndexOf('')"),
      nxt_string("16") },

    { nxt_string("'abc abc абвгдежз'.lastIndexOf('', 12)"),
      nxt_string("12") },

    { nxt_string("''.lastIndexOf('')"),
      nxt_string("0") },

    { nxt_string("''.includes('')"),
      nxt_string("true") },

    { nxt_string("'12345'.includes()"),
      nxt_string("false") },

    { nxt_string("'абвгдеёжзийклмнопрстуфхцчшщъыьэюя'.includes('лмно', 10)"),
      nxt_string("true") },

    { nxt_string("'абв абв абвгдежз'.includes('абвгд', 7)"),
      nxt_string("true") },

    { nxt_string("'абв абв абвгдежз'.includes('абвгд', 8)"),
      nxt_string("true") },

    { nxt_string("'абв абв абвгдежз'.includes('абвгд', 9)"),
      nxt_string("false") },

    { nxt_string("''.startsWith('')"),
      nxt_string("true") },

    { nxt_string("'12345'.startsWith()"),
      nxt_string("false") },

    { nxt_string("'abc'.startsWith('abc')"),
      nxt_string("true") },

    { nxt_string("'abc'.startsWith('abc', 1)"),
      nxt_string("false") },

    { nxt_string("'abc'.startsWith('abc', -1)"),
      nxt_string("true") },

    { nxt_string("'абв абв абвгдежз'.startsWith('абвгд', 8)"),
      nxt_string("true") },

    { nxt_string("'абв абв абвгдежз'.startsWith('абвгд', 9)"),
      nxt_string("false") },

    { nxt_string("''.endsWith('')"),
      nxt_string("true") },

    { nxt_string("'12345'.endsWith()"),
      nxt_string("false") },

    { nxt_string("'abc'.endsWith('abc')"),
      nxt_string("true") },

    { nxt_string("'abc'.endsWith('abc', 4)"),
      nxt_string("true") },

    { nxt_string("'abc'.endsWith('abc', 1)"),
      nxt_string("false") },

    { nxt_string("'abc'.endsWith('abc', -1)"),
      nxt_string("true") },

    { nxt_string("'абв абв абвгдежз'.endsWith('абвгд', 13)"),
      nxt_string("true") },

    { nxt_string("'абв абв абвгдежз'.endsWith('абвгд', 14)"),
      nxt_string("false") },

    { nxt_string("'ABC'.toLowerCase()"),
      nxt_string("abc") },

    { nxt_string("'ΑΒΓ'.toLowerCase()"),
      nxt_string("αβγ") },

    { nxt_string("'АБВ'.toLowerCase()"),
      nxt_string("абв") },

    { nxt_string("'abc'.toUpperCase()"),
      nxt_string("ABC") },

    { nxt_string("'αβγ'.toUpperCase()"),
      nxt_string("ΑΒΓ") },

    { nxt_string("'абв'.toUpperCase()"),
      nxt_string("АБВ") },

    { nxt_string("var a = [], code;"
                 "for (code = 0; code <= 1114111; code++) {"
                 "    var s = String.fromCharCode(code);"
                 "    var n = s.toUpperCase();"
                 "    if (s != n && s != n.toLowerCase())"
                 "        a.push(code);"
                 "} a"),
      nxt_string("181,305,383,453,456,459,498,837,962,976,977,981,982,1008,1009,1013,7835,8126") },

    { nxt_string("var a = [], code;"
                 "for (code = 0; code <= 1114111; code++) {"
                 "    var s = String.fromCharCode(code);"
                 "    var n = s.toLowerCase();"
                 "    if (s != n && s != n.toUpperCase())"
                 "        a.push(code);"
                 "} a"),
      nxt_string("304,453,456,459,498,1012,7838,8486,8490,8491") },

    { nxt_string("'abc'.trim()"),
      nxt_string("abc") },

    { nxt_string("''.trim()"),
      nxt_string("") },

    { nxt_string("'    '.trim()"),
      nxt_string("") },

    { nxt_string("'abc  '.trim()"),
      nxt_string("abc") },

    { nxt_string("'   abc'.trim()"),
      nxt_string("abc") },

    { nxt_string("'   abc  '.trim()"),
      nxt_string("abc") },

    { nxt_string("'абв  '.trim()"),
      nxt_string("абв") },

    { nxt_string("'   абв'.trim()"),
      nxt_string("абв") },

    { nxt_string("'   абв  '.trim()"),
      nxt_string("абв") },

    { nxt_string("'\\u2029abc\\uFEFF\\u2028'.trim()"),
      nxt_string("abc") },

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

    { nxt_string("var r = { toString: function() { return '45' } };"
                 "'123456'.search(r)"),
      nxt_string("3") },

    { nxt_string("var r = { toString: function() { return 45 } };"
                 "'123456'.search(r)"),
      nxt_string("3") },

    { nxt_string("var r = { toString: function() { return /45/ } };"
                 "'123456'.search(r)"),
      nxt_string("TypeError: Cannot convert object to primitive value") },

    { nxt_string("var r = { toString: function() { return /34/ },"
                 "          valueOf:  function() { return 45 } };"
                 "'123456'.search(r)"),
      nxt_string("3") },

    { nxt_string("'abcdefgh'.replace()"),
      nxt_string("abcdefgh") },

    { nxt_string("'abcdefgh'.replace('d')"),
      nxt_string("abcundefinedefgh") },

    { nxt_string("'abcdefgh'.replace('d', undefined)"),
      nxt_string("abcundefinedefgh") },

    { nxt_string("'abcdefgh'.replace('d', null)"),
      nxt_string("abcnullefgh") },

    { nxt_string("'abcdefgh'.replace('d', 1)"),
      nxt_string("abc1efgh") },

    { nxt_string("'abcdefghdijklm'.replace('d', 'X')"),
      nxt_string("abcXefghdijklm") },

    { nxt_string("'абвгдежгийклм'.replace('г', 'Г')"),
      nxt_string("абвГдежгийклм") },

    { nxt_string("'abcdefghdijklm'.replace('d',"
                 "   function(m, o, s) { return '|'+s+'|'+o+'|'+m+'|' })"),
      nxt_string("abc|abcdefghdijklm|3|d|efghdijklm") },

    { nxt_string("'abcdefgh'.replace('', 'X')"),
      nxt_string("Xabcdefgh") },

    { nxt_string("'abcdefghdijklm'.replace(/d/, 'X')"),
      nxt_string("abcXefghdijklm") },

    { nxt_string("'abcdefghdijklm'.replace(/d/,"
                 "   function(m, o, s) { return '|'+s+'|'+o+'|'+m+'|' })"),
      nxt_string("abc|abcdefghdijklm|3|d|efghdijklm") },

    { nxt_string("'abcdefghdijklm'.replace(/(d)/,"
                 "   function(m, p, o, s)"
                       "{ return '|'+s+'|'+o+'|'+m+'|'+p+'|' })"),
      nxt_string("abc|abcdefghdijklm|3|d|d|efghdijklm") },

    { nxt_string("'abcdefghdijklm'.replace(/x/, 'X')"),
      nxt_string("abcdefghdijklm") },

    { nxt_string("'abcdefghdijklm'.replace(/x/,"
                 "   function(m, o, s) { return '|'+s+'|'+o+'|'+m+'|' })"),
      nxt_string("abcdefghdijklm") },

    { nxt_string("'абвгдежгийклм'.replace(/г/, 'Г')"),
      nxt_string("абвГдежгийклм") },

    { nxt_string("'abcdefghdijklm'.replace(/d/g, 'X')"),
      nxt_string("abcXefghXijklm") },

    { nxt_string("'абвгдежгийклм'.replace(/г/g, 'Г')"),
      nxt_string("абвГдежГийклм") },

    { nxt_string("'abc12345#$*%'.replace(/([^\\d]*)(\\d*)([^\\w]*)/,"
                 "   function(match, p1, p2, p3) {"
                 "     return [p1, p2, p3].join('-')})"),
      nxt_string("abc-12345-#$*%") },

    { nxt_string("'ABCDEFGHDIJKLM'.replace(/[A-Z]/g,"
                 "   function(match) { return '-' + match.toLowerCase() })"),
      nxt_string("-a-b-c-d-e-f-g-h-d-i-j-k-l-m") },

    { nxt_string("'abcdbe'.replace(/(b)/g, '$')"),
      nxt_string("a$cd$e") },

    { nxt_string("'abcdbe'.replace(/(b)/g, '$2$23')"),
      nxt_string("a$2$23cd$2$23e") },

    { nxt_string("'abcdbe'.replace(/(b)/g, '$2$23X$$Y')"),
      nxt_string("a$2$23X$Ycd$2$23X$Ye") },

    { nxt_string("'abcdbe'.replace('b', '|$`X$\\'|')"),
      nxt_string("a|aXcdbe|cdbe") },

    { nxt_string("'abcdbe'.replace(/b/, '|$`X$\\'|')"),
      nxt_string("a|aXcdbe|cdbe") },

    { nxt_string("'abcdbefbgh'.replace(/b/g, '|$`X$\\'|')"),
      nxt_string("a|aXcdbefbgh|cd|abcdXefbgh|ef|abcdbefXgh|gh") },

    { nxt_string("'abc12345#$*%'.replace(/([^\\d]*)(\\d*)([^\\w]*)/,"
                 "                       '$1-$2-$3')"),
      nxt_string("abc-12345-#$*%") },

    { nxt_string("'$1,$2'.replace(/(\\$(\\d))/g, '$$1-$1$2')"),
      nxt_string("$1-$11,$1-$22") },

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

    { nxt_string("var r = { toString: function() { return '45' } };"
                 "'123456'.match(r)"),
      nxt_string("45") },

    { nxt_string("var r = { toString: function() { return 45 } };"
                 "'123456'.match(r)"),
      nxt_string("45") },

    { nxt_string("var r = { toString: function() { return /45/ } };"
                 "'123456'.match(r)"),
      nxt_string("TypeError: Cannot convert object to primitive value") },

    { nxt_string("var r = { toString: function() { return /34/ },"
                 "          valueOf:  function() { return 45 } };"
                 "'123456'.match(r)"),
      nxt_string("45") },

    { nxt_string("''.match(/^$/)"),
      nxt_string("") },

    { nxt_string("''.match(/^$/g)"),
      nxt_string("") },

    { nxt_string("'abcdefgh'.match(/def/)"),
      nxt_string("def") },

    { nxt_string("'abc abc abc'.match('abc')"),
      nxt_string("abc") },

    { nxt_string("'abc abc abc'.match(/abc/)"),
      nxt_string("abc") },

    { nxt_string("'abc abc abc'.match(/abc/g)"),
      nxt_string("abc,abc,abc") },

    { nxt_string("'abc ABC aBc'.match(/abc/ig)"),
      nxt_string("abc,ABC,aBc") },

    { nxt_string("var a = 'α'.match(/α/g)[0] + 'α';"
                 "a +' '+ a.length"),
      nxt_string("αα 2") },

    { nxt_string("var a = '\\u00CE\\u00B1'.toBytes().match(/α/g)[0] + 'α';"
                 "a +' '+ a.length"),
      nxt_string("αα 4") },

    { nxt_string("'abc'.split()"),
      nxt_string("abc") },

    { nxt_string("'abc'.split(undefined)"),
      nxt_string("abc") },

    { nxt_string("''.split('').length"),
      nxt_string("1") },

    { nxt_string("'abc'.split('')"),
      nxt_string("a,b,c") },

    { nxt_string("'abc'.split('abc')"),
      nxt_string(",") },

    { nxt_string("'a bc def'.split(' ')"),
      nxt_string("a,bc,def") },

    { nxt_string("'a bc  def'.split(' ')"),
      nxt_string("a,bc,,def") },

    { nxt_string("'a bc  def'.split(' ', 3)"),
      nxt_string("a,bc,") },

    { nxt_string("'abc'.split('abc')"),
      nxt_string(",") },

    { nxt_string("'ab'.split('123')"),
      nxt_string("ab") },

    { nxt_string("''.split(/0/).length"),
      nxt_string("1") },

    { nxt_string("'abc'.split(/(?:)/)"),
      nxt_string("a,b,c") },

    { nxt_string("'a bc def'.split(/ /)"),
      nxt_string("a,bc,def") },

    { nxt_string("'a bc  def'.split(/ /)"),
      nxt_string("a,bc,,def") },

    { nxt_string("'abc'.split(/abc/)"),
      nxt_string(",") },

    { nxt_string("'0123456789'.split('').reverse().join('')"),
      nxt_string("9876543210") },

    { nxt_string("'abc'.repeat(3)"),
      nxt_string("abcabcabc") },

    { nxt_string("'абв'.repeat(3)"),
      nxt_string("абвабвабв") },

    { nxt_string("''.repeat(3)"),
      nxt_string("") },

    { nxt_string("'abc'.repeat(0)"),
      nxt_string("") },

    { nxt_string("'abc'.repeat(NaN)"),
      nxt_string("") },

    { nxt_string("'abc'.repeat(Infinity)"),
      nxt_string("RangeError") },

    { nxt_string("'abc'.repeat(-1)"),
      nxt_string("RangeError") },

    { nxt_string("''.repeat(-1)"),
      nxt_string("RangeError") },

    { nxt_string("'a'.repeat(2147483647)"),
      nxt_string("RangeError") },

    { nxt_string("'a'.repeat(2147483648)"),
      nxt_string("RangeError") },

    { nxt_string("'a'.repeat(Infinity)"),
      nxt_string("RangeError") },

    { nxt_string("'a'.repeat(NaN)"),
      nxt_string("") },

    { nxt_string("''.repeat(2147483646)"),
      nxt_string("") },

    /* ES6: "". */
    { nxt_string("''.repeat(2147483647)"),
      nxt_string("RangeError") },

    { nxt_string("''.repeat(2147483648)"),
      nxt_string("RangeError") },

    { nxt_string("''.repeat(Infinity)"),
      nxt_string("RangeError") },

    { nxt_string("''.repeat(NaN)"),
      nxt_string("") },

    { nxt_string("'abc'.padStart(7)"),
      nxt_string("    abc") },

    { nxt_string("'абв'.padStart(7)"),
      nxt_string("    абв") },

    { nxt_string("'abc'.padStart(3)"),
      nxt_string("abc") },

    { nxt_string("'абв'.padStart(0)"),
      nxt_string("абв") },

    { nxt_string("'abc'.padStart(NaN)"),
      nxt_string("abc") },

    { nxt_string("'abc'.padStart(2147483647)"),
      nxt_string("RangeError") },

    { nxt_string("'abc'.padStart(2147483646, '')"),
      nxt_string("abc") },

    { nxt_string("''.padStart(0, '')"),
      nxt_string("") },

    { nxt_string("'1'.padStart(5, 0)"),
      nxt_string("00001") },

    { nxt_string("''.padStart(1, 'я')"),
      nxt_string("я") },

    { nxt_string("'abc'.padStart(6, NaN)"),
      nxt_string("NaNabc") },

    { nxt_string("'abc'.padStart(11, 123)"),
      nxt_string("12312312abc") },

    { nxt_string("'abc'.padStart(6, 12345)"),
      nxt_string("123abc") },

    { nxt_string("'абв'.padStart(6, 'эюя')"),
      nxt_string("эюяабв") },

    { nxt_string("'абв'.padStart(4, 'эюя')"),
      nxt_string("эабв") },

    { nxt_string("'абв'.padStart(7, 'эюя')"),
      nxt_string("эюяэабв") },

    { nxt_string("'абв'.padStart(10, 'эю')"),
      nxt_string("эюэюэюэабв") },

    { nxt_string("'1234'.padEnd(4)"),
      nxt_string("1234") },

    { nxt_string("'1234'.padEnd(-1)"),
      nxt_string("1234") },

    { nxt_string("'я'.padEnd(1)"),
      nxt_string("я") },

    { nxt_string("'1234'.padEnd(5)"),
      nxt_string("1234 ") },

    { nxt_string("'я'.padEnd(6)"),
      nxt_string("я     ") },

    { nxt_string("'я'.padEnd(2147483647)"),
      nxt_string("RangeError") },

    { nxt_string("'я'.padEnd(2147483646, '')"),
      nxt_string("я") },

    { nxt_string("''.padEnd(0, '')"),
      nxt_string("") },

    { nxt_string("'эю'.padEnd(3, 'я')"),
      nxt_string("эюя") },

    { nxt_string("''.padEnd(1, 0)"),
      nxt_string("0") },

    { nxt_string("'1234'.padEnd(8, 'abcd')"),
      nxt_string("1234abcd") },

    { nxt_string("'1234'.padEnd(10, 'abcd')"),
      nxt_string("1234abcdab") },

    { nxt_string("'1234'.padEnd(7, 'abcd')"),
      nxt_string("1234abc") },

    { nxt_string("'абв'.padEnd(5, 'ГД')"),
      nxt_string("абвГД") },

    { nxt_string("'абв'.padEnd(4, 'ГДУ')"),
      nxt_string("абвГ") },

    { nxt_string("'абвг'.padEnd(10, 'ДЕЖЗ')"),
      nxt_string("абвгДЕЖЗДЕ") },

    { nxt_string("String.bytesFrom({})"),
      nxt_string("TypeError: value must be a string or array") },

    { nxt_string("String.bytesFrom([1, 2, 0.23, '5', 'A']).toString('hex')"),
      nxt_string("0102000500") },

    { nxt_string("String.bytesFrom([NaN, Infinity]).toString('hex')"),
      nxt_string("0000") },

    { nxt_string("String.bytesFrom('', 'hex')"),
      nxt_string("") },

    { nxt_string("String.bytesFrom('00aabbcc', 'hex').toString('hex')"),
      nxt_string("00aabbcc") },

    { nxt_string("String.bytesFrom('deadBEEF##', 'hex').toString('hex')"),
      nxt_string("deadbeef") },

    { nxt_string("String.bytesFrom('aa0', 'hex').toString('hex')"),
      nxt_string("aa") },

    { nxt_string("String.bytesFrom('', 'base64')"),
      nxt_string("") },

    { nxt_string("String.bytesFrom('#', 'base64')"),
      nxt_string("") },

    { nxt_string("String.bytesFrom('QQ==', 'base64')"),
      nxt_string("A") },

    { nxt_string("String.bytesFrom('QQ', 'base64')"),
      nxt_string("A") },

    { nxt_string("String.bytesFrom('QUI=', 'base64')"),
      nxt_string("AB") },

    { nxt_string("String.bytesFrom('QUI', 'base64')"),
      nxt_string("AB") },

    { nxt_string("String.bytesFrom('QUJD', 'base64')"),
      nxt_string("ABC") },

    { nxt_string("String.bytesFrom('QUJDRA==', 'base64')"),
      nxt_string("ABCD") },

    { nxt_string("String.bytesFrom('', 'base64url')"),
      nxt_string("") },

    { nxt_string("String.bytesFrom('QQ', 'base64url')"),
      nxt_string("A") },

    { nxt_string("String.bytesFrom('QUI', 'base64url')"),
      nxt_string("AB") },

    { nxt_string("String.bytesFrom('QUJD', 'base64url')"),
      nxt_string("ABC") },

    { nxt_string("String.bytesFrom('QUJDRA', 'base64url')"),
      nxt_string("ABCD") },

    { nxt_string("String.bytesFrom('QUJDRA#', 'base64url')"),
      nxt_string("ABCD") },

    { nxt_string("encodeURI()"),
      nxt_string("undefined")},

    { nxt_string("encodeURI('012абв')"),
      nxt_string("012%D0%B0%D0%B1%D0%B2")},

    { nxt_string("encodeURI('~}|{`_^]\\\\[@?>=<;:/.-,+*)(\\\'&%$#\"! ')"),
      nxt_string("~%7D%7C%7B%60_%5E%5D%5C%5B@?%3E=%3C;:/.-,+*)('&%25$#%22!%20")},

    { nxt_string("encodeURIComponent('~}|{`_^]\\\\[@?>=<;:/.-,+*)(\\\'&%$#\"! ')"),
      nxt_string("~%7D%7C%7B%60_%5E%5D%5C%5B%40%3F%3E%3D%3C%3B%3A%2F.-%2C%2B*)('%26%25%24%23%22!%20")},

    { nxt_string("decodeURI()"),
      nxt_string("undefined")},

    { nxt_string("decodeURI('%QQ')"),
      nxt_string("URIError")},

    { nxt_string("decodeURI('%')"),
      nxt_string("URIError")},

    { nxt_string("decodeURI('%0')"),
      nxt_string("URIError")},

    { nxt_string("decodeURI('%00')"),
      nxt_string("\0")},

    { nxt_string("decodeURI('%3012%D0%B0%D0%B1%D0%B2')"),
      nxt_string("012абв")},

    { nxt_string("decodeURI('%7e%7d%7c%7b%60%5f%5e%5d%5c%5b%40%3f%3e%3d%3c%3b%3a%2f%2e%2c%2b%2a%29%28%27%26%25%24%23%22%21%20')"),
      nxt_string("~}|{`_^]\\[%40%3f>%3d<%3b%3a%2f.%2c%2b*)('%26%%24%23\"! ")},

    { nxt_string("decodeURIComponent('%7e%7d%7c%7b%60%5f%5e%5d%5c%5b%40%3f%3e%3d%3c%3b%3a%2f%2e%2c%2b%2a%29%28%27%26%25%24%23%22%21%20')"),
      nxt_string("~}|{`_^]\\[@?>=<;:/.,+*)('&%$#\"! ")},

    { nxt_string("decodeURI('%41%42%43').length"),
      nxt_string("3")},

    { nxt_string("decodeURI('%D0%B0%D0%B1%D0%B2').length"),
      nxt_string("3")},

    { nxt_string("decodeURI('%80%81%82').length"),
      nxt_string("3")},

    /* Functions. */

    { nxt_string("return"),
      nxt_string("SyntaxError: Illegal return statement in 1") },

    { nxt_string("{return}"),
      nxt_string("SyntaxError: Illegal return statement in 1") },

    { nxt_string("function f() { return f() } f()"),
      nxt_string("RangeError: Maximum call stack size exceeded") },

    { nxt_string("function () { } f()"),
      nxt_string("SyntaxError: Unexpected token \"(\" in 1") },

    { nxt_string("function f() { }"),
      nxt_string("undefined") },

    { nxt_string("function f() { }; f.length"),
      nxt_string("0") },

    { nxt_string("function f() { }; f.length = 1"),
      nxt_string("TypeError: Cannot assign to read-only property 'length' of function") },

    { nxt_string("function f(a,b) { }; f.length"),
      nxt_string("2") },

    { nxt_string("function f(a,b) { }; var ff = f.bind(f, 1); ff.length"),
      nxt_string("1") },

    { nxt_string("JSON.parse.length"),
      nxt_string("2") },

    { nxt_string("JSON.parse.bind(JSON, '[]').length"),
      nxt_string("1") },

    { nxt_string("var o = {}; o.hasOwnProperty.length"),
      nxt_string("1") },

    { nxt_string("var x; function f() { }"),
      nxt_string("undefined") },

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

    { nxt_string("function f() { return 1\n 2 } f()"),
      nxt_string("1") },

    { nxt_string("(function f() { return 2.toString(); })()"),
      nxt_string("SyntaxError: Unexpected token \"toString\" in 1") },

    { nxt_string("(function f() { return 2..toString(); })()"),
      nxt_string("2") },

    { nxt_string("function f(a) { if (a) return 'OK' } f(1)+f(0)"),
      nxt_string("OKundefined") },

    { nxt_string("function f(a) { if (a) return 'OK'; } f(1)+f(0)"),
      nxt_string("OKundefined") },

    { nxt_string("function f(a) { if (a) return 'OK';; } f(1)+f(0)"),
      nxt_string("OKundefined") },

    { nxt_string("var a = 1; a()"),
      nxt_string("TypeError: number is not a function") },

    { nxt_string("var o = {a:1}; o.a()"),
      nxt_string("TypeError: 'a' is not a function") },

    { nxt_string("(function(){})()"),
      nxt_string("undefined") },

    { nxt_string("var q = 1; function x(a, b, c) { q = a } x(5); q"),
      nxt_string("5") },

    { nxt_string("function x(a) { while (a < 2) a++; return a + 1 } x(1) "),
      nxt_string("3") },

    { nxt_string("(function(){(function(){(function(){(function(){"
                    "(function(){(function(){(function(){})})})})})})})"),
      nxt_string("SyntaxError: The maximum function nesting level is \"5\" in 1") },

    { nxt_string("Function.prototype.toString = function () {return 'X'};"
                 "eval"),
      nxt_string("X") },

    { nxt_string("var o = {f:function(x){ return x**2}}; o.f\n(2)"),
      nxt_string("4") },

    { nxt_string("var o = {f:function(x){ return x**2}}; o\n.f\n(2)"),
      nxt_string("4") },

    { nxt_string("var o = {f:function(x){ return x**2}}; o\n.\nf\n(2)"),
      nxt_string("4") },

    { nxt_string("function f(x){ return x**2}; [f(2)\n, f\n(2),\nf\n(\n2),\nf\n(\n2\n)]"),
      nxt_string("4,4,4,4") },

    { nxt_string("function f (x){ return x**2}; f\n(2)"),
      nxt_string("4") },

    { nxt_string("function f (x){ return x**2}; f\n(\n2)"),
      nxt_string("4") },

    { nxt_string("function f (x){ return x**2}; f\n(\n2\n)"),
      nxt_string("4") },

    { nxt_string("function f (x){ return x**2}; f\n(2\n)"),
      nxt_string("4") },

    { nxt_string("function f (x){ return x**2}; f(2\n)"),
      nxt_string("4") },

    /* Recursive factorial. */

    { nxt_string("function f(a) {"
                 "    if (a > 1)"
                 "        return a * f(a - 1)\n"
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

    /* Nested functions and closures. */

    { nxt_string("function f() { var x = 4; "
                 "function g() { return x }; return g(); } f()"),
      nxt_string("4") },

    { nxt_string("function f(a) { function g(b) { return a + b } return g }"
                 "var k = f('a'); k('b')"),
      nxt_string("ab") },

    { nxt_string("function f(a) { return function(b) { return a + b } }"
                 "var k = f('a'); k('b')"),
      nxt_string("ab") },

    { nxt_string("function f(a) { return function(b) { return a + b } }"
                 "var k = f('a'), m = f('b'); k('c') + m('d')"),
      nxt_string("acbd") },

    { nxt_string("function f(a) { return "
                 "function(b) { return function(c) { return a + b + c } } }"
                 "var g = f('a'), k = g('b'), m = g('c'); k('d') + m('e')"),
      nxt_string("abdace") },

    { nxt_string("function f(a) {"
                 "function g() { return a }; return g; }"
                 "var y = f(4); y()"),
      nxt_string("4") },

    { nxt_string("function f() { var x = 4; "
                 "return function() { return x } }"
                 "var y = f(); y()"),
      nxt_string("4") },

    { nxt_string("function f() { var x = 4;"
                 "function g() { if (1) { return 2 + x; } }; return g }"
                 "var y = f(); y()"),
      nxt_string("6") },

    { nxt_string("var x; var y = 4;"
                 "function f() { function h() { x = 3; return y; } }"),
      nxt_string("undefined") },

    { nxt_string("function f() {"
                 "    var a = 'a';"
                 "    if (0) { a = 'b' };"
                 "    function f2() { return a };"
                 "    return f2"
                 "};"
                 "f()()"),
      nxt_string("a") },

    { nxt_string("function f() {"
                 "    var a = 'a'; "
                 "    if (0) { if (0) {a = 'b'} };"
                 "    function f2() { return a };"
                 "    return f2"
                 "};"
                 "f()()"),
      nxt_string("a") },

    { nxt_string("function f() { var a = f2(); }"),
      nxt_string("ReferenceError: \"f2\" is not defined in 1") },

    { nxt_string("(function(){ function f() {return f}; return f()})()"),
      nxt_string("[object Function]") },

    { nxt_string("var a = ''; "
                 "function f(list) {"
                 "    function add(v) {a+=v};"
                 "    list.forEach(function(v) {add(v)});"
                 "};"
                 "f(['a', 'b', 'c']); a"),
      nxt_string("abc") },

    { nxt_string("var l = [];"
                 "var f = function() { "
                 "    function f2() { l.push(f); l.push(f2); }; "
                 "    l.push(f); l.push(f2); "
                 "    f2(); "
                 "}; "
                 "f(); "
                 "l.every(function(v) {return typeof v == 'function'})"),
      nxt_string("true") },

    { nxt_string("var l = [];"
                 "function baz() {"
                 "  function foo(v) {"
                 "     function bar() { foo(0); }"
                 "     l.push(v);"
                 "     if (v === 1) { bar(); }"
                 "  }"
                 "  foo(1);"
                 "}; baz(); l"),
      nxt_string("1,0") },

    { nxt_string("var gen = (function(){  "
                 "           var s = 0; "
                 "           return { inc: function() {s++}, "
                 "                    s: function() {return s} };});"
                 "var o1 = gen(); var o2 = gen();"
                 "[o1.s(),o2.s(),o1.inc(),o1.s(),o2.s(),o2.inc(),o1.s(),o2.s()]"),
      nxt_string("0,0,,1,0,,1,1") },

    /* Recursive fibonacci. */

    { nxt_string("function fibo(n) {"
                 "    if (n > 1)"
                 "        return fibo(n-1) + fibo(n-2)\n"
                 "     return 1"
                 "}"
                 "fibo(10)"),
      nxt_string("89") },

    { nxt_string("function fibo(n) {"
                 "    if (n > 1)"
                 "        return fibo(n-1) + fibo(n-2)\n"
                 "     return '.'"
                 "}"
                 "fibo(10).length"),
      nxt_string("89") },

    { nxt_string("function fibo(n) {"
                 "    if (n > 1)"
                 "        return fibo(n-1) + fibo(n-2)\n"
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

    { nxt_string("function f(a) { return a + 1 } var b = f(2); b"),
      nxt_string("3") },

    { nxt_string("var f = function(a) { a *= 2; return a }; f(10)"),
      nxt_string("20") },

    { nxt_string("var f = function b(a) { a *= 2; return a }; f(10)"),
      nxt_string("20") },

    { nxt_string("var f = function b(a) { a *= 2; return a }; b(10)"),
      nxt_string("ReferenceError: \"b\" is not defined in 1") },

    { nxt_string("var f; f = function(a) { a *= 2; return a }; f(10)"),
      nxt_string("20") },

    { nxt_string("var f; f = function b(a) { a *= 2; return a }; f(10)"),
      nxt_string("20") },

    { nxt_string("var a, f = a = function(a) { a *= 2; return a }; f(10)"),
      nxt_string("20") },

    { nxt_string("var a, f = a = function(a) { a *= 2; return a }; a(10)"),
      nxt_string("20") },

    { nxt_string("var f = function b(a) { a *= 2; return a } = 5"),
      nxt_string("ReferenceError: Invalid left-hand side in assignment in 1") },

    { nxt_string("function a() { return { x:2} }; var b = a(); b.x"),
      nxt_string("2") },

    { nxt_string("var a = {}; function f(a) { return a + 1 } a.b = f(2); a.b"),
      nxt_string("3") },

    { nxt_string("(function(x) { return x + 1 })(2)"),
      nxt_string("3") },

    { nxt_string("(function(x) { return x + 1 }(2))"),
      nxt_string("3") },

    { nxt_string("var a = function() { return 1 }(); a"),
      nxt_string("1") },

    { nxt_string("var a = (function() { return 1 })(); a"),
      nxt_string("1") },

    { nxt_string("var a = (function(a) { return a + 1 })(2); a"),
      nxt_string("3") },

    { nxt_string("var a = (function(a) { return a + 1 }(2)); a"),
      nxt_string("3") },

    { nxt_string("var a = +function(a) { return a + 1 }(2); a"),
      nxt_string("3") },

    { nxt_string("var a = -function(a) { return a + 1 }(2); a"),
      nxt_string("-3") },

    { nxt_string("var a = !function(a) { return a + 1 }(2); a"),
      nxt_string("false") },

    { nxt_string("var a = ~function(a) { return a + 1 }(2); a"),
      nxt_string("-4") },

    { nxt_string("var a = void function(a) { return a + 1 }(2); a"),
      nxt_string("undefined") },

    { nxt_string("var a = true && function(a) { return a + 1 }(2); a"),
      nxt_string("3") },

    { nxt_string("var a; a = 0, function(a) { return a + 1 }(2); a"),
      nxt_string("0") },

    { nxt_string("var a = (0, function(a) { return a + 1 }(2)); a"),
      nxt_string("3") },

    { nxt_string("var a = 0, function(a) { return a + 1 }(2); a"),
      nxt_string("SyntaxError: Unexpected token \"function\" in 1") },

    { nxt_string("var a = (0, function(a) { return a + 1 }(2)); a"),
      nxt_string("3") },

    { nxt_string("var a = +function f(a) { return a + 1 }(2);"
                 "var b = f(5); a"),
      nxt_string("ReferenceError: \"f\" is not defined in 1") },

    { nxt_string("var o = { f: function(a) { return a * 2 } }; o.f(5)"),
      nxt_string("10") },

    { nxt_string("var o = {}; o.f = function(a) { return a * 2 }; o.f(5)"),
      nxt_string("10") },

    { nxt_string("var o = { x: 1, f: function() { return this.x } }; o.f()"),
      nxt_string("1") },

    { nxt_string("var o = { x: 1, f: function(a) { return this.x += a } };"
                 "o.f(5) +' '+ o.x"),
      nxt_string("6 6") },

    { nxt_string("var f = function(a) { return 3 }; f.call()"),
      nxt_string("3") },

    { nxt_string("var f = function(a) { return this }; f.call(5)"),
      nxt_string("5") },

    { nxt_string("var f = function(a, b) { return this + a }; f.call(5, 1)"),
      nxt_string("6") },

    { nxt_string("var f = function(a, b) { return this + a + b };"
                 "f.call(5, 1, 2)"),
      nxt_string("8") },

    { nxt_string("var f = function(a) { return 3 }; f.apply()"),
      nxt_string("3") },

    { nxt_string("var f = function(a) { return this }; f.apply(5)"),
      nxt_string("5") },

    { nxt_string("var f = function(a) { return this + a }; f.apply(5, 1)"),
      nxt_string("TypeError: second argument is not an array") },

    { nxt_string("var f = function(a, b) { return this + a + b };"
                 "f.apply(5, [1, 2])"),
      nxt_string("8") },

    { nxt_string("var f = function(a, b) { return this + a + b };"
                 "f.apply(5, [1, 2], 3)"),
      nxt_string("8") },

    { nxt_string("var a = function() { return 1 } + ''; a"),
      nxt_string("[object Function]") },

    { nxt_string("''.concat.call()"),
      nxt_string("TypeError: 'this' argument is null or undefined") },

    { nxt_string("''.concat.call('a', 'b', 'c')"),
      nxt_string("abc") },

    { nxt_string("''.concat.call('a')"),
      nxt_string("a") },

    { nxt_string("''.concat.call('a', [ 'b', 'c' ])"),
      nxt_string("ab,c") },

    { nxt_string("''.concat.call('a', [ 'b', 'c' ], 'd')"),
      nxt_string("ab,cd") },

    { nxt_string("''.concat.apply()"),
      nxt_string("TypeError: 'this' argument is null or undefined") },

    { nxt_string("''.concat.apply('a')"),
      nxt_string("a") },

    { nxt_string("''.concat.apply('a', 'b')"),
      nxt_string("TypeError: second argument is not an array") },

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
      nxt_string("TypeError: cannot convert undefined to object") },

    { nxt_string("[].slice.call()"),
      nxt_string("TypeError: cannot convert undefined to object") },

    { nxt_string("function f(a) {} ; var a = f; var b = f; a === b"),
      nxt_string("true") },

    { nxt_string("function f() {} ; f.toString()"),
      nxt_string("[object Function]") },

    { nxt_string("function f() {}; f"),
      nxt_string("[object Function]") },

    { nxt_string("function f() {}; f = f + 1; f"),
      nxt_string("[object Function]1") },

    { nxt_string("function a() { return 1 }"
                 "function b() { return a }"
                 "function c() { return b }"
                 "c()()()"),
      nxt_string("1") },

#if 0
    { nxt_string("function f() {}; f += 1; f"),
      nxt_string("[object Function]1") },
#endif

    { nxt_string("function f() {}; function g() { return f }; g()"),
      nxt_string("[object Function]") },

    { nxt_string("function f(a) { return this+a }; var a = f; a.call('0', 1)"),
      nxt_string("01") },

    { nxt_string("function f(a) { return this+a }; f.call('0', 1)"),
      nxt_string("01") },

    { nxt_string("function f(a) { return this+a };"
                 "function g(f, a, b) { return f.call(a, b) }; g(f, '0', 1)"),
      nxt_string("01") },

    { nxt_string("function f(a) { return this+a };"
                 "var o = { g: function (f, a, b) { return f.call(a, b) } };"
                 "o.g(f, '0', 1)"),
      nxt_string("01") },

    { nxt_string("var concat = ''.concat; concat(1,2,3)"),
      nxt_string("TypeError: 'this' argument is null or undefined") },

    { nxt_string("var concat = ''.concat; concat.call(1,2,3)"),
      nxt_string("123") },

    { nxt_string("var concat = ''.concat; concat.yes = 'OK';"
                 "concat.call(1,2,3, concat.yes)"),
      nxt_string("123OK") },

    { nxt_string("var f = function(a, b) { return this + a + b };"
                 "var b = f.bind('1'); b('2', '3')"),
      nxt_string("123") },

    { nxt_string("var f = function(a, b) { return this + a + b };"
                 "var b = f.bind('1', '2'); b('3')"),
      nxt_string("123") },

    { nxt_string("var f = function(a, b) { return this + a + b };"
                 "var b = f.bind('1', 2, '3'); b()"),
      nxt_string("123") },

    { nxt_string("var f = function(a, b) { return this + a + b };"
                 "var b = f.bind('1'); b.call('0', '2', '3')"),
      nxt_string("123") },

    { nxt_string("var f = function(a, b) { return this + a + b };"
                 "var b = f.bind('1', '2'); b.call('0', '3')"),
      nxt_string("123") },

    { nxt_string("var f = function(a, b) { return this + a + b };"
                 "var b = f.bind('1', '2', '3'); b.call('0')"),
      nxt_string("123") },

    { nxt_string("var f = function(a, b) { return this + a + b };"
                 "var b = f.bind('1', '2', '3'); b.call()"),
      nxt_string("123") },

    { nxt_string("var f = function(a, b) { return this + a + b };"
                 "var b = f.bind('1'); b.apply('0', ['2', '3'])"),
      nxt_string("123") },

    { nxt_string("var f = function(a, b) { return this + a + b };"
                 "var b = f.bind('1', '2'); b.apply('0', ['3'])"),
      nxt_string("123") },

    { nxt_string("var f = function(a, b) { return this + a + b };"
                 "var b = f.bind('1', '2', '3'); b.apply('0')"),
      nxt_string("123") },

    { nxt_string("var f = function(a, b) { return this + a + b };"
                 "var b = f.bind('1', '2', '3'); b.apply()"),
      nxt_string("123") },

    { nxt_string("function f(a, b) { return this + a + b }"
                 "var b = f.bind('1', '2', '3'); b.apply()"),
      nxt_string("123") },

    { nxt_string("function F(a, b) { this.a = a + b }"
                 "var o = new F(1, 2);"
                 "o.a"),
      nxt_string("3") },

    { nxt_string("function F(a, b) { this.a = a + b; return { a: 7 } }"
                 "var o = new F(1, 2);"
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

    { nxt_string("function F() { return }"
                 "var o = new F();"
                 "o.__proto__ === F.prototype"),
      nxt_string("true") },

    { nxt_string("var f = { F: function(){} }; var o = new f.F();"
                 "o.__proto__ === f.F.prototype"),
      nxt_string("true") },

    { nxt_string("function F(){}; typeof F.prototype"),
      nxt_string("object") },

    { nxt_string("var F = function (){}; typeof F.prototype"),
      nxt_string("object") },

    { nxt_string("new decodeURI('%00')"),
      nxt_string("TypeError: function is not a constructor")},

    { nxt_string("new ''.toString"),
      nxt_string("TypeError: function is not a constructor")},

    { nxt_string("function F() { return Number }"
                 "var o = new (F())(5);"
                 "typeof o +' '+ o"),
      nxt_string("object 5") },

    { nxt_string("function F() { return Number }"
                 "var o = new (F());"
                 "typeof o +' '+ o"),
      nxt_string("object 0") },

    { nxt_string("var o = new function F() { return Number }()(5);"
                 "typeof o +' '+ o"),
      nxt_string("number 5") },

    { nxt_string("var o = new (function F() { return Number }())(5);"
                 "typeof o +' '+ o"),
      nxt_string("object 5") },

    { nxt_string("var o = new (new function F() { return Number }())(5);"
                 "typeof o +' '+ o"),
      nxt_string("object 5") },

    { nxt_string("var o = new new function F() { return Number }()(5);"
                 "typeof o +' '+ o"),
      nxt_string("object 5") },

    { nxt_string("var b; function F(x) { return {a:x} }"
                 "function G(y) { b = y; return F }"
                 "var o = new G(3)(5);"
                 "b + ' ' + o.a"),
      nxt_string("3 5") },

    { nxt_string("var b; function F(x) { return {a:x} }"
                 "function G(y) { b = y; return F }"
                 "var o = new (new G(3))(5);"
                 "b + ' ' + o.a"),
      nxt_string("3 5") },

    { nxt_string("var b; function F(x) { return {a:x} }"
                 "function G(y) { b = y; return F }"
                 "var o = new new G(3)(5);"
                 "b + ' ' + o.a"),
      nxt_string("3 5") },

    { nxt_string("var b; function F(x) { return {a:x} }"
                 "var g = { G: function (y) { b = y; return F } };"
                 "var o = new new g.G(3)(5);"
                 "b + ' ' + o.a"),
      nxt_string("3 5") },

    { nxt_string("function a() { return function(x) { return x + 1 } }"
                 "var b = a(); b(2)"),
      nxt_string("3") },

    /* arguments object. */

    { nxt_string("var arguments"),
      nxt_string("SyntaxError: Identifier \"arguments\" is forbidden in var declaration in 1") },

    { nxt_string("for (var arguments in []) {}"),
      nxt_string("SyntaxError: Identifier \"arguments\" is forbidden in for-in var declaration in 1") },

    { nxt_string("function arguments(){}"),
      nxt_string("SyntaxError: Identifier \"arguments\" is forbidden in function declaration in 1") },

    { nxt_string("(function () {arguments = [];})"),
      nxt_string("SyntaxError: Identifier \"arguments\" is forbidden as left-hand in assignment in 1") },

    { nxt_string("(function(){return arguments[0];})(1,2,3)"),
      nxt_string("1") },

    { nxt_string("(function(){return arguments[2];})(1,2,3)"),
      nxt_string("3") },

    { nxt_string("(function(){return arguments[3];})(1,2,3)"),
      nxt_string("undefined") },

    { nxt_string("(function(a,b,c){return a;})(1,2,3)"),
      nxt_string("1") },

    { nxt_string("(function(a,b,c){arguments[0] = 4; return a;})(1,2,3)"),
      nxt_string("1") },

    { nxt_string("(function(a,b,c){a = 4; return arguments[0];})(1,2,3)"),
      nxt_string("1") },

    { nxt_string("function check(v) {if (v == false) {throw TypeError('Too few arguments')}}; "
                 "function f() {check(arguments.length > 1); return 1}; f()"),
      nxt_string("TypeError: Too few arguments") },

    { nxt_string("function check(v) {if (v == false) {throw TypeError('Too few arguments')}}; "
                 "function f() {check(arguments.length > 1); return 1}; f(1,2)"),
      nxt_string("1") },

    { nxt_string("(function(a,b){delete arguments[0]; return arguments[0]})(1,1)"),
      nxt_string("undefined") },

    { nxt_string("(function(){return arguments.length;})()"),
      nxt_string("0") },

    { nxt_string("(function(){return arguments.length;})(1,2,3)"),
      nxt_string("3") },

    { nxt_string("(function(){arguments.length = 1; return arguments.length;})(1,2,3)"),
      nxt_string("1") },

    { nxt_string("(function(){return arguments.callee;})()"),
      nxt_string("TypeError: 'caller', 'callee' properties may not be accessed") },

    { nxt_string("(function(){return arguments.caller;})()"),
      nxt_string("TypeError: 'caller', 'callee' properties may not be accessed") },

    { nxt_string("function sum() { var args = Array.prototype.slice.call(arguments); "
                 "return args.reduce(function(prev, curr) {return prev + curr})};"
                 "[sum(1), sum(1,2), sum(1,2,3), sum(1,2,3,4)]"),
      nxt_string("1,3,6,10") },

    { nxt_string("function concat(sep) { var args = Array.prototype.slice.call(arguments, 1); "
                 "return args.join(sep)};"
                 "[concat('.',1,2,3), concat('+',1,2,3,4)]"),
      nxt_string("1.2.3,1+2+3+4") },

    /* Scopes. */

    { nxt_string("function f(x) { a = x } var a; f(5); a"),
      nxt_string("5") },

    { nxt_string("function f(x) { var a = x } var a = 2; f(5); a"),
      nxt_string("2") },

    { nxt_string("function f(a) { return a } var a = '2'; a + f(5)"),
      nxt_string("25") },

    { nxt_string("for (var i = 0; i < 5; i++); i"),
      nxt_string("5") },

    { nxt_string("for (var i = 0, j; i < 5; i++); j"),
      nxt_string("undefined") },

    { nxt_string("for (var i = 0, j, k = 3; i < 5; i++); k"),
      nxt_string("3") },

    { nxt_string("var o = { a: 1, b: 2, c: 3 }, s = ''; "
                 "for (var i in o) { s += i }; s"),
      nxt_string("abc") },

    { nxt_string("var o = { a: 1, b: 2, c: 3 }; for (var i in o); i"),
      nxt_string("c") },

    { nxt_string("var o = {}; i = 7; for (var i in o); i"),
      nxt_string("7") },

    { nxt_string("var a = [1,2,3]; for (var i in a); i"),
      nxt_string("2") },

    /* RegExp. */

    { nxt_string("/./x"),
      nxt_string("SyntaxError: Invalid RegExp flags \"x\" in 1") },

    { nxt_string("/"),
      nxt_string("SyntaxError: Unterminated RegExp \"/\" in 1") },

    { nxt_string("/(/.test('')"),
      nxt_string("SyntaxError: pcre_compile(\"(\") failed: missing ) in 1") },

    { nxt_string("/+/.test('')"),
      nxt_string("SyntaxError: pcre_compile(\"+\") failed: nothing to repeat at \"+\" in 1") },

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

    { nxt_string("var r = /3/g; r.exec('123') +' '+ r.exec('3')"),
      nxt_string("3 null") },

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
                "var a = r.exec('The Quick Brown Fox Jumps Over The Lazy Dog');"
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

    { nxt_string("new RegExp('', 'x')"),
      nxt_string("SyntaxError: Invalid RegExp flags \"x\"") },

    { nxt_string("[0].map(RegExp().toString)"),
      nxt_string("TypeError: 'this' argument is not a regexp") },

    /* Non-standard ECMA-262 features. */

    /* 0x10400 is not a surrogate pair of 0xD801 and 0xDC00. */

    { nxt_string("var chars = '𐐀'; chars.length +' '+ chars.charCodeAt(0)"),
      nxt_string("1 66560") },

    /* es5id: 6.1, 0x104A0 is not a surrogate pair of 0xD801 and 0xDCA0. */

    { nxt_string("var chars = '𐒠'; chars.length +' '+ chars.charCodeAt(0)"),
      nxt_string("1 66720") },

    /* Error object. */

    { nxt_string("Error()"),
      nxt_string("Error") },

    { nxt_string("new Error()"),
      nxt_string("Error") },

    { nxt_string("Error('e')"),
      nxt_string("Error: e") },

    { nxt_string("var e = Error('e'); e.name = 'E'; e"),
      nxt_string("E: e") },

    { nxt_string("var e = Error('e'); e.name = ''; e"),
      nxt_string("e") },

    { nxt_string("var e = Error(); e.name = ''; e"),
      nxt_string("") },

    { nxt_string("var e = Error(); e.name = ''; e.message = 'e'; e"),
      nxt_string("e") },

    { nxt_string("Error('e').name + ': ' + Error('e').message"),
      nxt_string("Error: e") },

    { nxt_string("Error(1)"),
      nxt_string("Error: 1") },

    { nxt_string("Error.__proto__ == Function.prototype"),
      nxt_string("true") },

    { nxt_string("Error.prototype.name"),
      nxt_string("Error") },

    { nxt_string("Error.prototype.message"),
      nxt_string("") },

    { nxt_string("Error.prototype.constructor == Error"),
      nxt_string("true") },

    { nxt_string("Error().__proto__ == Error.prototype"),
      nxt_string("true") },

    { nxt_string("Error().__proto__.__proto__ == Object.prototype"),
      nxt_string("true") },

    { nxt_string("EvalError('e')"),
      nxt_string("EvalError: e") },

    { nxt_string("InternalError('e')"),
      nxt_string("InternalError: e") },

    { nxt_string("RangeError('e')"),
      nxt_string("RangeError: e") },

    { nxt_string("ReferenceError('e')"),
      nxt_string("ReferenceError: e") },

    { nxt_string("SyntaxError('e')"),
      nxt_string("SyntaxError: e") },

    { nxt_string("TypeError('e')"),
      nxt_string("TypeError: e") },

    { nxt_string("URIError('e')"),
      nxt_string("URIError: e") },

    { nxt_string("MemoryError('e')"),
      nxt_string("MemoryError") },

    { nxt_string("EvalError('e').name + ': ' + EvalError('e').message"),
      nxt_string("EvalError: e") },

    { nxt_string("InternalError('e').name + ': ' + InternalError('e').message"),
      nxt_string("InternalError: e") },

    { nxt_string("RangeError('e').name + ': ' + RangeError('e').message"),
      nxt_string("RangeError: e") },

    { nxt_string("ReferenceError('e').name + ': ' + ReferenceError('e').message"),
      nxt_string("ReferenceError: e") },

    { nxt_string("SyntaxError('e').name + ': ' + SyntaxError('e').message"),
      nxt_string("SyntaxError: e") },

    { nxt_string("TypeError('e').name + ': ' + TypeError('e').message"),
      nxt_string("TypeError: e") },

    { nxt_string("URIError('e').name + ': ' + URIError('e').message"),
      nxt_string("URIError: e") },

    { nxt_string("var e = EvalError('e'); e.name = 'E'; e"),
      nxt_string("E: e") },

    { nxt_string("var e = InternalError('e'); e.name = 'E'; e"),
      nxt_string("E: e") },

    { nxt_string("var e = RangeError('e'); e.name = 'E'; e"),
      nxt_string("E: e") },

    { nxt_string("var e = ReferenceError('e'); e.name = 'E'; e"),
      nxt_string("E: e") },

    { nxt_string("var e = SyntaxError('e'); e.name = 'E'; e"),
      nxt_string("E: e") },

    { nxt_string("var e = TypeError('e'); e.name = 'E'; e"),
      nxt_string("E: e") },

    { nxt_string("var e = URIError('e'); e.name = 'E'; e"),
      nxt_string("E: e") },

    /* Memory object is immutable. */

    { nxt_string("var e = MemoryError('e'); e.name = 'E'"),
      nxt_string("TypeError: Cannot add property 'name', object is not extensible") },

    { nxt_string("EvalError.prototype.name"),
      nxt_string("EvalError") },

    { nxt_string("InternalError.prototype.name"),
      nxt_string("InternalError") },

    { nxt_string("RangeError.prototype.name"),
      nxt_string("RangeError") },

    { nxt_string("ReferenceError.prototype.name"),
      nxt_string("ReferenceError") },

    { nxt_string("SyntaxError.prototype.name"),
      nxt_string("SyntaxError") },

    { nxt_string("TypeError.prototype.name"),
      nxt_string("TypeError") },

    { nxt_string("URIError.prototype.name"),
      nxt_string("URIError") },

    { nxt_string("MemoryError.prototype.name"),
      nxt_string("InternalError") },

    { nxt_string("EvalError.prototype.message"),
      nxt_string("") },

    { nxt_string("InternalError.prototype.message"),
      nxt_string("") },

    { nxt_string("RangeError.prototype.message"),
      nxt_string("") },

    { nxt_string("ReferenceError.prototype.message"),
      nxt_string("") },

    { nxt_string("SyntaxError.prototype.message"),
      nxt_string("") },

    { nxt_string("TypeError.prototype.message"),
      nxt_string("") },

    { nxt_string("URIError.prototype.message"),
      nxt_string("") },

    { nxt_string("MemoryError.prototype.message"),
      nxt_string("") },

    { nxt_string("EvalError.prototype.constructor == EvalError"),
      nxt_string("true") },

    { nxt_string("RangeError.prototype.constructor == RangeError"),
      nxt_string("true") },

    { nxt_string("ReferenceError.prototype.constructor == ReferenceError"),
      nxt_string("true") },

    { nxt_string("SyntaxError.prototype.constructor == SyntaxError"),
      nxt_string("true") },

    { nxt_string("TypeError.prototype.constructor == TypeError"),
      nxt_string("true") },

    { nxt_string("URIError.prototype.constructor == URIError"),
      nxt_string("true") },

    { nxt_string("EvalError().__proto__ == EvalError.prototype"),
      nxt_string("true") },

    { nxt_string("RangeError().__proto__ == RangeError.prototype"),
      nxt_string("true") },

    { nxt_string("ReferenceError().__proto__ == ReferenceError.prototype"),
      nxt_string("true") },

    { nxt_string("SyntaxError().__proto__ == SyntaxError.prototype"),
      nxt_string("true") },

    { nxt_string("TypeError().__proto__ == TypeError.prototype"),
      nxt_string("true") },

    { nxt_string("URIError().__proto__ == URIError.prototype"),
      nxt_string("true") },

    { nxt_string("EvalError().__proto__.__proto__ == Error.prototype"),
      nxt_string("true") },

    { nxt_string("RangeError().__proto__.__proto__ == Error.prototype"),
      nxt_string("true") },

    { nxt_string("ReferenceError().__proto__.__proto__ == Error.prototype"),
      nxt_string("true") },

    { nxt_string("SyntaxError().__proto__.__proto__ == Error.prototype"),
      nxt_string("true") },

    { nxt_string("TypeError().__proto__.__proto__ == Error.prototype"),
      nxt_string("true") },

    { nxt_string("URIError().__proto__.__proto__ == Error.prototype"),
      nxt_string("true") },

    { nxt_string("MemoryError().__proto__ == MemoryError.prototype"),
      nxt_string("true") },

    { nxt_string("MemoryError().__proto__.__proto__ == Error.prototype"),
      nxt_string("true") },

    { nxt_string("typeof Error()"),
      nxt_string("object") },

    { nxt_string("typeof EvalError()"),
      nxt_string("object") },

    { nxt_string("typeof InternalError()"),
      nxt_string("object") },

    { nxt_string("typeof RangeError()"),
      nxt_string("object") },

    { nxt_string("typeof ReferenceError()"),
      nxt_string("object") },

    { nxt_string("typeof SyntaxError()"),
      nxt_string("object") },

    { nxt_string("typeof TypeError()"),
      nxt_string("object") },

    { nxt_string("typeof URIError()"),
      nxt_string("object") },

    { nxt_string("typeof MemoryError()"),
      nxt_string("object") },

    /* Exceptions. */

    { nxt_string("throw null"),
      nxt_string("null") },

    { nxt_string("var a; try { throw null } catch (e) { a = e } a"),
      nxt_string("null") },

    { nxt_string("var a; try { throw Error('e') } catch (e) { a = e.message } a"),
      nxt_string("e") },

    { nxt_string("var a; try { NaN.toString(NaN) } catch (e) { a = e.name } a"),
      nxt_string("RangeError") },

    { nxt_string("try { throw null } catch (e) { throw e }"),
      nxt_string("null") },

    { nxt_string("try { throw Error('e') } catch (e) { throw Error(e.message + '2') }"),
      nxt_string("Error: e2") },

    { nxt_string("try { throw null } catch (null) { throw e }"),
      nxt_string("SyntaxError: Unexpected token \"null\" in 1") },

    { nxt_string("'a'.f()"),
      nxt_string("TypeError: 'f' is not a function") },

    { nxt_string("1..f()"),
      nxt_string("TypeError: 'f' is not a function") },

    { nxt_string("try {}"),
      nxt_string("SyntaxError: Missing catch or finally after try in 1") },

    { nxt_string("try{}catch(a[]"),
      nxt_string("SyntaxError: Unexpected token \"[\" in 1") },

    { nxt_string("function f(a) {return a;}; "
                 "function thrower() {throw TypeError('Oops')}; "
                 "f(thrower())"),
      nxt_string("TypeError: Oops") },

    { nxt_string("var a = 0; try { a = 5 }"
                 "catch (e) { a = 9 } finally { a++ } a"),
      nxt_string("6") },

    { nxt_string("var a = 0; try { throw 3 }"
                 "catch (e) { a = e } finally { a++ } a"),
      nxt_string("4") },

    { nxt_string("var a = 0; try { throw 3 }"
                 "catch (e) { throw e + 1 } finally { a++ }"),
      nxt_string("4") },

    { nxt_string("var a = 0; try { throw 3 }"
                 "catch (e) { a = e } finally { throw a }"),
      nxt_string("3") },

    { nxt_string("try { throw null } catch (e) { } finally { }"),
      nxt_string("undefined") },

    { nxt_string("var a = 0; try { throw 3 }"
                 "catch (e) { throw 4 } finally { throw a }"),
      nxt_string("0") },

    { nxt_string("var a = 0; try { a = 5 } finally { a++ } a"),
      nxt_string("6") },

    { nxt_string("var a = 0; try { throw 5 } finally { a++ }"),
      nxt_string("5") },

    { nxt_string("var a = 0; try { a = 5 } finally { throw 7 }"),
      nxt_string("7") },

    { nxt_string("function f(a) {"
                 "   if (a > 1) return f(a - 1);"
                 "   throw 9; return a }"
                 "var a = 0; try { a = f(5); a++ } catch(e) { a = e } a"),
      nxt_string("9") },

    { nxt_string("var a; try { try { throw 5 } catch (e) { a = e } throw 3 }"
                 "       catch(x) { a += x } a"),
      nxt_string("8") },

    { nxt_string("throw\nnull"),
      nxt_string("SyntaxError: Illegal newline after throw in 2") },

    { nxt_string("var o = { valueOf: function() { return '3' } }; --o"),
      nxt_string("2") },

    { nxt_string("var o = { valueOf: function() { return [3] } }; --o"),
      nxt_string("NaN") },

    { nxt_string("var o = { valueOf: function() { return '3' } }; 10 - o"),
      nxt_string("7") },

    { nxt_string("var o = { valueOf: function() { return [3] } }; 10 - o"),
      nxt_string("NaN") },

    { nxt_string("var o = { toString: function() { return 'OK' } }; 'o:' + o"),
      nxt_string("o:OK") },

    { nxt_string("var o = { toString: function() { return [1] } }; o"),
      nxt_string("TypeError: Cannot convert object to primitive value") },

    { nxt_string("var o = { toString: function() { return [1] } }; 'o:' + o"),
      nxt_string("TypeError: Cannot convert object to primitive value") },

    { nxt_string("var a = { valueOf: function() { return '3' } };"
                 "var b = { toString: function() { return 10 - a + 'OK' } };"
                 "var c = { toString: function() { return b + 'YES' } };"
                 "'c:' + c"),
      nxt_string("c:7OKYES") },

    { nxt_string("[1,2,3].valueOf()"),
      nxt_string("1,2,3") },

    { nxt_string("var o = { valueOf: function() { return 'OK' } };"
                 "o.valueOf()"),
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
      nxt_string("TypeError: right argument is not a function") },

    { nxt_string("[] instanceof Array"),
      nxt_string("true") },

    { nxt_string("[] instanceof Object"),
      nxt_string("true") },

    { nxt_string("/./ instanceof RegExp"),
      nxt_string("true") },

    { nxt_string("/./ instanceof Object"),
      nxt_string("true") },

    /* global this. */

    { nxt_string("this"),
      nxt_string("[object Object]") },

    { nxt_string("this.a = 1; this.a"),
      nxt_string("1") },

    { nxt_string("this.undefined = 42"),
      nxt_string("TypeError: Cannot assign to read-only property 'undefined' of object") },

    { nxt_string("this.Infinity = 42"),
      nxt_string("TypeError: Cannot assign to read-only property 'Infinity' of object") },

    { nxt_string("this.NaN = 42"),
      nxt_string("TypeError: Cannot assign to read-only property 'NaN' of object") },

    { nxt_string("typeof this.undefined"),
      nxt_string("undefined") },

    { nxt_string("typeof this.Infinity"),
      nxt_string("number") },

    { nxt_string("this.Infinity + 1"),
      nxt_string("Infinity") },

    { nxt_string("typeof this.NaN"),
      nxt_string("number") },

    { nxt_string("this.NaN + 1"),
      nxt_string("NaN") },

    { nxt_string("if (1) {new this}"),
      nxt_string("TypeError: object is not a function") },

    { nxt_string("if (1) {this()}"),
      nxt_string("TypeError: object is not a function") },

    { nxt_string("var ex; try {new this} catch (e) {ex = e}; ex"),
      nxt_string("TypeError: object is not a function") },

    { nxt_string("var ex; try {({}) instanceof this} catch (e) {ex = e}; ex"),
      nxt_string("TypeError: right argument is not a function") },

    { nxt_string("Function.call(this, 'var x / = 1;')"),
      nxt_string("InternalError: Not implemented") },

    { nxt_string("njs"),
      nxt_string("[object Object]") },

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

    { nxt_string("var o = Object([]); Object.prototype.toString.call(o)"),
      nxt_string("[object Array]") },

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

    { nxt_string("Object.prototype.__proto__.f()"),
      nxt_string("TypeError: cannot get property 'f' of undefined") },

    { nxt_string("Object.prototype.toString.call(Object.prototype)"),
      nxt_string("[object Object]") },

    { nxt_string("Object.prototype.toString.call(new Array)"),
      nxt_string("[object Array]") },

    { nxt_string("Object.prototype.toString.call(new URIError)"),
      nxt_string("[object Error]") },

    { nxt_string("Object.prototype"),
      nxt_string("[object Object]") },

    { nxt_string("Object.constructor === Function"),
      nxt_string("true") },

    { nxt_string("({}).__proto__ === Object.prototype"),
      nxt_string("true") },

    { nxt_string("({}).__proto__ = 1"),
      nxt_string("TypeError: Cannot assign to read-only property '__proto__' of object") },

    { nxt_string("({}).__proto__.constructor === Object"),
      nxt_string("true") },

    { nxt_string("({}).constructor === Object"),
      nxt_string("true") },

    { nxt_string("({}).constructor()"),
      nxt_string("[object Object]") },

    { nxt_string("var a = Object.__proto__; a()"),
      nxt_string("undefined") },

    { nxt_string("Object.__proto__()"),
      nxt_string("undefined") },

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
      nxt_string("RangeError: Invalid array length") },

    { nxt_string("var a = Array(2.5)"),
      nxt_string("RangeError: Invalid array length") },

    { nxt_string("var a = Array(NaN)"),
      nxt_string("RangeError: Invalid array length") },

    { nxt_string("var a = Array(Infinity)"),
      nxt_string("RangeError: Invalid array length") },

    { nxt_string("var a = Array(1111111111)"),
      nxt_string("MemoryError") },

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

    { nxt_string("Object.prototype.toString.call(Array.prototype)"),
      nxt_string("[object Array]") },

    { nxt_string("Array.prototype"),
      nxt_string("") },

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

    { nxt_string("([]).constructor()"),
      nxt_string("") },

    { nxt_string("Boolean()"),
      nxt_string("false") },

    { nxt_string("new Boolean()"),
      nxt_string("false") },

    { nxt_string("new Boolean"),
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

    { nxt_string("typeof new Boolean"),
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

    { nxt_string("Object.prototype.toString.call(Boolean.prototype)"),
      nxt_string("[object Boolean]") },

    { nxt_string("Boolean.prototype"),
      nxt_string("false") },

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

    { nxt_string("new Number()"),
      nxt_string("0") },

    { nxt_string("new Number"),
      nxt_string("0") },

    { nxt_string("new Number(undefined)"),
      nxt_string("NaN") },

    { nxt_string("new Number(null)"),
      nxt_string("0") },

    { nxt_string("new Number(true)"),
      nxt_string("1") },

    { nxt_string("new Number(false)"),
      nxt_string("0") },

    { nxt_string("Number(undefined)"),
      nxt_string("NaN") },

    { nxt_string("Number(null)"),
      nxt_string("0") },

    { nxt_string("Number(true)"),
      nxt_string("1") },

    { nxt_string("Number(false)"),
      nxt_string("0") },

    { nxt_string("Number(123)"),
      nxt_string("123") },

    { nxt_string("Number('123')"),
      nxt_string("123") },

    { nxt_string("Number('0.'+'1'.repeat(128))"),
      nxt_string("0.1111111111111111") },

    { nxt_string("Number('1'.repeat(128))"),
      nxt_string("1.1111111111111113e+127") },

    { nxt_string("Number('1'.repeat(129))"),
      nxt_string("1.1111111111111112e+128") },

    { nxt_string("Number('1'.repeat(129))"),
      nxt_string("1.1111111111111112e+128") },

    { nxt_string("Number('1'.repeat(129)+'e-100')"),
      nxt_string("1.1111111111111112e+28") },

    { nxt_string("Number('1'.repeat(310))"),
      nxt_string("Infinity") },

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

    { nxt_string("typeof new Number"),
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

    { nxt_string("Object.prototype.toString.call(Number.prototype)"),
      nxt_string("[object Number]") },

    { nxt_string("Number.prototype"),
      nxt_string("0") },

    { nxt_string("Number.constructor === Function"),
      nxt_string("true") },

    { nxt_string("0..__proto__ === Number.prototype"),
      nxt_string("true") },

    { nxt_string("var n = Number(1); n.__proto__ === Number.prototype"),
      nxt_string("true") },

    { nxt_string("var n = new Number(1); n.__proto__ === Number.prototype"),
      nxt_string("true") },

    { nxt_string("Number.isFinite()"),
      nxt_string("false") },

    { nxt_string("Number.isFinite(123)"),
      nxt_string("true") },

    { nxt_string("Number.isFinite('123')"),
      nxt_string("false") },

    { nxt_string("Number.isFinite(Infinity)"),
      nxt_string("false") },

    { nxt_string("Number.isFinite(NaN)"),
      nxt_string("false") },

    { nxt_string("Number.isInteger()"),
      nxt_string("false") },

    { nxt_string("Number.isInteger('123')"),
      nxt_string("false") },

    { nxt_string("Number.isInteger(123)"),
      nxt_string("true") },

    { nxt_string("Number.isInteger(-123.0)"),
      nxt_string("true") },

    { nxt_string("Number.isInteger(123.4)"),
      nxt_string("false") },

    { nxt_string("Number.isInteger(Infinity)"),
      nxt_string("false") },

    { nxt_string("Number.isInteger(NaN)"),
      nxt_string("false") },

    { nxt_string("Number.isSafeInteger()"),
      nxt_string("false") },

    { nxt_string("Number.isSafeInteger('123')"),
      nxt_string("false") },

    { nxt_string("Number.isSafeInteger(9007199254740991)"),
      nxt_string("true") },

    { nxt_string("Number.isSafeInteger(-9007199254740991.0)"),
      nxt_string("true") },

    { nxt_string("Number.isSafeInteger(9007199254740992)"),
      nxt_string("false") },

    { nxt_string("Number.isSafeInteger(-9007199254740992.0)"),
      nxt_string("false") },

    { nxt_string("Number.isSafeInteger(123.4)"),
      nxt_string("false") },

    { nxt_string("Number.isSafeInteger(Infinity)"),
      nxt_string("false") },

    { nxt_string("Number.isSafeInteger(-Infinity)"),
      nxt_string("false") },

    { nxt_string("Number.isSafeInteger(NaN)"),
      nxt_string("false") },

    { nxt_string("Number.isNaN()"),
      nxt_string("false") },

    { nxt_string("Number.isNaN('NaN')"),
      nxt_string("false") },

    { nxt_string("Number.isNaN(NaN)"),
      nxt_string("true") },

    { nxt_string("Number.isNaN(123)"),
      nxt_string("false") },

    { nxt_string("Number.isNaN(Infinity)"),
      nxt_string("false") },

#if 0
    { nxt_string("parseFloat === Number.parseFloat"),
      nxt_string("true") },

    { nxt_string("parseInt === Number.parseInt"),
      nxt_string("true") },
#endif

    { nxt_string("String()"),
      nxt_string("") },

    { nxt_string("new String()"),
      nxt_string("") },

    { nxt_string("new String"),
      nxt_string("") },

    { nxt_string("String(123)"),
      nxt_string("123") },

    { nxt_string("new String(123)"),
      nxt_string("123") },

    { nxt_string("new String(123).length"),
      nxt_string("3") },

    { nxt_string("new String(123).toString()"),
      nxt_string("123") },

    { nxt_string("String([1,2,3])"),
      nxt_string("1,2,3") },

    { nxt_string("new String([1,2,3])"),
      nxt_string("1,2,3") },

    { nxt_string("var s = new String('αβ'); s.one = 1; 'one' in s"),
      nxt_string("true") },

    { nxt_string("var s = new String('αβ'); 'one' in s"),
      nxt_string("false") },

    { nxt_string("var s = new String('αβ'); s.one = 1; '1' in s"),
      nxt_string("true") },

    { nxt_string("var s = new String('αβ'); s.one = 1; 1 in s"),
      nxt_string("true") },

    { nxt_string("var s = new String('αβ'); s.one = 1; 2 in s"),
      nxt_string("false") },

    { nxt_string("var s = new String('αβ'); s[1]"),
      nxt_string("β") },

    { nxt_string("Object.create(new String('αβ'))[1]"),
      nxt_string("β") },

    { nxt_string("var s = new String('αβ'); s[1] = 'b'"),
      nxt_string("TypeError: Cannot assign to read-only property '1' of object string") },

    { nxt_string("var s = new String('αβ'); s[4] = 'ab'; s[4]"),
      nxt_string("ab") },


#if 0
    { nxt_string("Object.create(new String('αβ')).length"),
      nxt_string("2") },
#endif

    { nxt_string("var s = new String('αβ'); s.valueOf()[1]"),
      nxt_string("β") },

    { nxt_string("var o = { toString: function() { return 'OK' } };"
                 "String(o)"),
      nxt_string("OK") },

    { nxt_string("var o = { toString: function() { return 'OK' } };"
                 "new String(o)"),
      nxt_string("OK") },

    { nxt_string("typeof String('abc')"),
      nxt_string("string") },

    { nxt_string("typeof new String('abc')"),
      nxt_string("object") },

    { nxt_string("typeof new String"),
      nxt_string("object") },

    { nxt_string("String.name"),
      nxt_string("String") },

    { nxt_string("String.length"),
      nxt_string("1") },

    { nxt_string("String.__proto__ === Function.prototype"),
      nxt_string("true") },

    { nxt_string("Object.prototype.toString.call(String.prototype)"),
      nxt_string("[object String]") },

    { nxt_string("String.prototype"),
      nxt_string("") },

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

    { nxt_string("Object.prototype.toString.call(Function.prototype)"),
      nxt_string("[object Function]") },

    { nxt_string("Function.prototype"),
      nxt_string("[object Function]") },

    { nxt_string("Function.constructor === Function"),
      nxt_string("true") },

    { nxt_string("function f() {} f.__proto__ === Function.prototype"),
      nxt_string("true") },

    { nxt_string("Function()"),
      nxt_string("InternalError: Not implemented") },

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

    { nxt_string("Object.prototype.toString.call(RegExp.prototype)"),
      nxt_string("[object RegExp]") },

    { nxt_string("RegExp.prototype"),
      nxt_string("/(?:)/") },

    { nxt_string("RegExp.constructor === Function"),
      nxt_string("true") },

    { nxt_string("/./.__proto__ === RegExp.prototype"),
      nxt_string("true") },

    { nxt_string("toString()"),
      nxt_string("[object Undefined]") },

    { nxt_string("toString() + Object.prototype.toString"),
      nxt_string("[object Undefined][object Function]") },

#if 0
    { nxt_string("toString === Object.prototype.toString"),
      nxt_string("true") },

    { nxt_string("Object.prototype.toString.yes = 'OK'; toString.yes"),
      nxt_string("OK") },
#endif

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

    { nxt_string("Object.create()"),
      nxt_string("TypeError: prototype may only be an object or null: undefined") },

    { nxt_string("Object.create(1)"),
      nxt_string("TypeError: prototype may only be an object or null: number") },

    { nxt_string("var o = {a:1, b:2, c:3};"
                 "Object.keys(o)"),
      nxt_string("a,b,c") },

    { nxt_string("var a = []; a.one = 7; Object.keys(a)"),
      nxt_string("one") },

    { nxt_string("var a = [,,]; a.one = 7; Object.keys(a)"),
      nxt_string("one") },

    { nxt_string("var a = [,6,,3]; a.one = 7; Object.keys(a)"),
      nxt_string("1,3,one") },

    { nxt_string("var o = {a:1,b:2}; delete o.a; Object.keys(o)"),
      nxt_string("b") },

    { nxt_string("Object.keys()"),
      nxt_string("TypeError: cannot convert undefined argument to object") },

    { nxt_string("Object.keys('αβZ')"),
      nxt_string("0,1,2") },

    { nxt_string("Object.keys(new String('αβZ'))"),
      nxt_string("0,1,2") },

    { nxt_string("var s = new String('αβZ'); s.a = 1; Object.keys(s)"),
      nxt_string("0,1,2,a") },

    { nxt_string("var r = new RegExp('αbc'); r.a = 1; Object.keys(r)"),
      nxt_string("a") },

    { nxt_string("Object.keys(Object.create(new String('abc')))"),
      nxt_string("") },

    { nxt_string("Object.keys(1)"),
      nxt_string("") },

    { nxt_string("Object.keys(true)"),
      nxt_string("") },

    { nxt_string("var o = {}; Object.defineProperty(o, 'a', {}); o.a"),
      nxt_string("undefined") },

    { nxt_string("Object.defineProperty({}, 'a', {value:1})"),
      nxt_string("[object Object]") },

    { nxt_string("var o = {}; Object.defineProperty(o, 'a', {value:1}); o.a"),
      nxt_string("1") },

    { nxt_string("var o = {a:1, c:2}; Object.defineProperty(o, 'b', {});"
                 "Object.keys(o)"),
      nxt_string("a,c") },

    { nxt_string("var o = {a:1, c:2};"
                 "Object.defineProperty(o, 'b', {enumerable:false});"
                 "Object.keys(o)"),
      nxt_string("a,c") },

    { nxt_string("var o = {a:1, c:3};"
                 "Object.defineProperty(o, 'b', {enumerable:true});"
                 "Object.keys(o)"),
      nxt_string("a,c,b") },

    { nxt_string("var o = {}; Object.defineProperty(o, 'a', {}); o.a = 1"),
      nxt_string("TypeError: Cannot assign to read-only property 'a' of object") },

    { nxt_string("var o = {}; Object.defineProperty(o, 'a', {writable:false}); o.a = 1"),
      nxt_string("TypeError: Cannot assign to read-only property 'a' of object") },

    { nxt_string("var o = {}; Object.defineProperty(o, 'a', {writable:true});"
                 "o.a = 1; o.a"),
      nxt_string("1") },

    { nxt_string("var o = {};"
                 "Object.defineProperty(o, 'a', {value:1}); delete o.a"),
      nxt_string("TypeError: Cannot delete property 'a' of object") },

    { nxt_string("var o = {};"
                 "Object.defineProperty(o, 'a', {value:1, configurable:true});"
                 "delete o.a; o.a"),
      nxt_string("undefined") },

    { nxt_string("var o = {};"
                 "Object.defineProperty(o, 'a', {value:1, configurable:false});"
                 "delete o.a"),
      nxt_string("TypeError: Cannot delete property 'a' of object") },

    { nxt_string("var o = {};"
                 "Object.defineProperty(o, 'a', Object.create({value:2})); o.a"),
      nxt_string("2") },

    { nxt_string("var o = {};"
                 "Object.defineProperty(o, 'a', {configurable:false});"
                 "Object.defineProperty(o, 'a', {configurable:true})"),
      nxt_string("TypeError: Cannot redefine property: 'a'") },

    { nxt_string("var o = {};"
                 "Object.defineProperty(o, 'a', {configurable:false});"
                 "Object.defineProperty(o, 'a', {enumerable:true})"),
      nxt_string("TypeError: Cannot redefine property: 'a'") },

    { nxt_string("var o = {};"
                 "Object.defineProperty(o, 'a', {configurable:false});"
                 "Object.defineProperty(o, 'a', {writable:true})"),
      nxt_string("TypeError: Cannot redefine property: 'a'") },

    { nxt_string("var o = {};"
                 "Object.defineProperty(o, 'a', {configurable:false});"
                 "Object.defineProperty(o, 'a', {enumerable:false}).a"),
      nxt_string("undefined") },

    { nxt_string("var o = {};"
                 "Object.defineProperty(o, 'a', {configurable:false});"
                 "Object.defineProperty(o, 'a', {}).a"),
      nxt_string("undefined") },

    { nxt_string("var o = {};"
                 "Object.defineProperty(o, 'a', {configurable:false, writable:true});"
                 "Object.defineProperty(o, 'a', {writable:false}).a"),
      nxt_string("undefined") },

    { nxt_string("var o = {};"
                 "Object.defineProperty(o, 'a', {configurable:true, writable:false});"
                 "Object.defineProperty(o, 'a', {writable:true}).a"),
      nxt_string("undefined") },

    { nxt_string("var o = {};"
                 "Object.defineProperty(o, 'a', {});"
                 "Object.defineProperty(o, 'a', {}).a"),
      nxt_string("undefined") },

    { nxt_string("var o = {};"
                 "Object.defineProperty(o, 'a', {value:1});"
                 "Object.defineProperty(o, 'a', {value:1}).a"),
      nxt_string("1") },

    { nxt_string("var o = {};"
                 "Object.defineProperty(o, 'a', {value:1});"
                 "Object.defineProperty(o, 'a', {value:2}).a"),
      nxt_string("TypeError: Cannot redefine property: 'a'") },

    { nxt_string("var o = {};"
                 "Object.defineProperty(o, 'a', {configurable:true});"
                 "Object.defineProperty(o, 'a', {value:1}).a"),
      nxt_string("1") },

    { nxt_string("var o = {};"
                 "Object.defineProperty(o, 'a', { configurable: true, value: 0 });"
                 "Object.defineProperty(o, 'a', { value: 1 });"
                 "Object.defineProperty(o, 'a', { configurable: false, value: 2 }).a"),
      nxt_string("2") },

    { nxt_string("var o = {}; Object.defineProperty()"),
      nxt_string("TypeError: cannot convert undefined argument to object") },

    { nxt_string("var o = {}; Object.defineProperty(o)"),
      nxt_string("TypeError: descriptor is not an object") },

    { nxt_string("var o = Object.defineProperties({}, {a:{value:1}}); o.a"),
      nxt_string("1") },

    { nxt_string("var o = Object.defineProperties({}, {a:{enumerable:true}, b:{enumerable:true}});"
                 "Object.keys(o)"),
      nxt_string("a,b") },

    { nxt_string("var desc = Object.defineProperty({b:{value:1, enumerable:true}}, 'a', {});"
                 "var o = Object.defineProperties({}, desc);"
                 "Object.keys(o)"),
      nxt_string("b") },

    { nxt_string("var o = {a:1}; delete o.a;"
                 "Object.defineProperty(o, 'a', { value: 1 }); o.a"),
      nxt_string("1") },

    { nxt_string("var o = {a:1}; delete o.a;"
                 "Object.defineProperty(o, 'a', { value: 1 }); o.a = 2; o.a"),
      nxt_string("TypeError: Cannot assign to read-only property 'a' of object") },

    { nxt_string("var o = {a:1}; delete o.a;"
                 "Object.defineProperty(o, 'a', { value: 1, writable:1 }); o.a = 2; o.a"),
      nxt_string("2") },

    { nxt_string("var o = {};"
                 "Object.defineProperty(o, new String('a'), { value: 1}); o.a"),
      nxt_string("1") },

    { nxt_string("var o = {};"
                 "Object.defineProperty(o, {toString:function(){return 'a'}}, { value: 1}); o.a"),
      nxt_string("1") },

    { nxt_string("var a = [1, 2];"
                 "Object.defineProperty(a, '1', {value: 5}); a[1]"),
      nxt_string("5") },

    { nxt_string("var a = [1, 2];"
                 "Object.defineProperty(a, '3', {}); njs.dump(a)"),
      nxt_string("[1,2,<empty>,undefined]") },

    { nxt_string("var a = [1, 2];"
                 "Object.defineProperty(a, 'length', {}); a"),
      nxt_string("1,2") },

    { nxt_string("var a = [1, 2];"
                 "Object.defineProperty(a, 'length', {value: 1}); a"),
      nxt_string("1") },

    { nxt_string("var a = [1, 2];"
                 "Object.defineProperty(a, 'length', {value: 5}); a"),
      nxt_string("1,2,,,") },

    { nxt_string("var o = Object.defineProperties({a:1}, {}); o.a"),
      nxt_string("1") },

    { nxt_string("Object.defineProperties(1, {})"),
      nxt_string("TypeError: cannot convert number argument to object") },

    { nxt_string("Object.defineProperties({}, 1)"),
      nxt_string("TypeError: descriptor is not an object") },

    { nxt_string("var o = {a:1}; o.hasOwnProperty('a')"),
      nxt_string("true") },

    { nxt_string("var o = Object.create({a:2}); o.hasOwnProperty('a')"),
      nxt_string("false") },

    { nxt_string("var o = {a:1}; o.hasOwnProperty('b')"),
      nxt_string("false") },

    { nxt_string("var a = []; a.hasOwnProperty('0')"),
      nxt_string("false") },

    { nxt_string("var a = [,,]; a.hasOwnProperty('0')"),
      nxt_string("false") },

    { nxt_string("var a = [3,,]; a.hasOwnProperty('0')"),
      nxt_string("true") },

    { nxt_string("var a = [,4]; a.hasOwnProperty('1')"),
      nxt_string("true") },

    { nxt_string("var a = [3,4]; a.hasOwnProperty('2')"),
      nxt_string("false") },

    { nxt_string("var a = [3,4]; a.one = 1; a.hasOwnProperty('one')"),
      nxt_string("true") },

    { nxt_string("var o = {a:1}; o.hasOwnProperty(o)"),
      nxt_string("false") },

    { nxt_string("var o = {a:1}; o.hasOwnProperty(1)"),
      nxt_string("false") },

    { nxt_string("var o = {a:1}; o.hasOwnProperty()"),
      nxt_string("false") },

    { nxt_string("[,].hasOwnProperty()"),
      nxt_string("false") },

    { nxt_string("[1,2].hasOwnProperty('len')"),
      nxt_string("false") },

    { nxt_string("[].hasOwnProperty('length')"),
      nxt_string("true") },

    { nxt_string("[1,2].hasOwnProperty('length')"),
      nxt_string("true") },

    { nxt_string("(new Array()).hasOwnProperty('length')"),
      nxt_string("true") },

    { nxt_string("(new Array(10)).hasOwnProperty('length')"),
      nxt_string("true") },

    { nxt_string("Object.valueOf.hasOwnProperty()"),
      nxt_string("false") },

    { nxt_string("1..hasOwnProperty('b')"),
      nxt_string("false") },

    { nxt_string("'s'.hasOwnProperty('b')"),
      nxt_string("false") },

    { nxt_string("var p = { a:5 }; var o = Object.create(p);"
                 "Object.getPrototypeOf(o) === p"),
      nxt_string("true") },

    { nxt_string("var p = { a:5 }; var o = Object.create(p);"
                 "Object.getPrototypeOf(o) === o.__proto__"),
      nxt_string("true") },

    { nxt_string("var o = Object.create(Object.prototype);"
                 "Object.getPrototypeOf(o) === Object.prototype"),
      nxt_string("true") },

    { nxt_string("Object.getPrototypeOf(1)"),
      nxt_string("TypeError: cannot convert number argument to object") },

    { nxt_string("Object.getPrototypeOf('a')"),
      nxt_string("TypeError: cannot convert string argument to object") },

    { nxt_string("var p = {}; var o = Object.create(p);"
                 "p.isPrototypeOf(o)"),
      nxt_string("true") },

    { nxt_string("var pp = {}; var p = Object.create(pp);"
                 "var o = Object.create(p);"
                 "pp.isPrototypeOf(o)"),
      nxt_string("true") },

    { nxt_string("var p = {}; var o = Object.create(p);"
                 "o.isPrototypeOf(p)"),
      nxt_string("false") },

    { nxt_string("var p = {}; var o = Object.create(p);"
                 "o.isPrototypeOf()"),
      nxt_string("false") },

    { nxt_string("Object.valueOf.isPrototypeOf()"),
      nxt_string("false") },

    { nxt_string("var p = {}; var o = Object.create(p);"
                 "o.isPrototypeOf(1)"),
      nxt_string("false") },

    { nxt_string("var p = {}; var o = Object.create(p);"
                 "1..isPrototypeOf(p)"),
      nxt_string("false") },

    { nxt_string("Object.getOwnPropertyDescriptor({a:1}, 'a').value"),
      nxt_string("1") },

    { nxt_string("Object.getOwnPropertyDescriptor({a:1}, 'a').configurable"),
      nxt_string("true") },

    { nxt_string("Object.getOwnPropertyDescriptor({a:1}, 'a').enumerable"),
      nxt_string("true") },

    { nxt_string("Object.getOwnPropertyDescriptor({a:1}, 'a').writable"),
      nxt_string("true") },

    { nxt_string("Object.getOwnPropertyDescriptor({a:1}, 'b')"),
      nxt_string("undefined") },

    { nxt_string("Object.getOwnPropertyDescriptor({}, 'a')"),
      nxt_string("undefined") },

    { nxt_string("Object.getOwnPropertyDescriptor(Object.create({a:1}), 'a')"),
      nxt_string("undefined") },

    { nxt_string("Object.getOwnPropertyDescriptor([3,4], '1').value"),
      nxt_string("4") },

    { nxt_string("Object.getOwnPropertyDescriptor([3,4], 1).value"),
      nxt_string("4") },

    { nxt_string("Object.getOwnPropertyDescriptor([], 'length').value"),
      nxt_string("0") },

    { nxt_string("Object.getOwnPropertyDescriptor([], '0')"),
      nxt_string("undefined") },

    { nxt_string("Object.getOwnPropertyDescriptor([1,2], '1').value"),
      nxt_string("2") },

    { nxt_string("Object.getOwnPropertyDescriptor([1,2], new String('1')).value"),
      nxt_string("2") },

    { nxt_string("Object.getOwnPropertyDescriptor({undefined:1}, void 0).value"),
      nxt_string("1") },

    { nxt_string("Object.getOwnPropertyDescriptor([1,2], 1).value"),
      nxt_string("2") },

    { nxt_string("Object.getOwnPropertyDescriptor([1,,,3], '1')"),
      nxt_string("undefined") },

    { nxt_string("Object.getOwnPropertyDescriptor([1,2], '3')"),
      nxt_string("undefined") },

    { nxt_string("JSON.stringify(Object.getOwnPropertyDescriptor([3,4], 'length'))"),
      nxt_string("{\"value\":2,\"configurable\":false,\"enumerable\":false,\"writable\":true}") },

    { nxt_string("Object.getOwnPropertyDescriptor(Array.of, 'length').value"),
      nxt_string("0") },

    { nxt_string("Object.getOwnPropertyDescriptor('αβγδ', '1').value"),
      nxt_string("β") },

    { nxt_string("Object.getOwnPropertyDescriptor(new String('αβγδ'), '1').value"),
      nxt_string("β") },

    { nxt_string("var s = new String('αβγδ'); s.a = 1;"
                 "Object.getOwnPropertyDescriptor(s, 'a').value"),
      nxt_string("1") },

    { nxt_string("JSON.stringify(Object.getOwnPropertyDescriptor('αβγδ', '2'))"),
      nxt_string("{\"value\":\"γ\",\"configurable\":false,\"enumerable\":true,\"writable\":false}") },

    { nxt_string("JSON.stringify(Object.getOwnPropertyDescriptor(new String('abc'), 'length'))"),
      nxt_string("{\"value\":3,\"configurable\":false,\"enumerable\":false,\"writable\":false}") },

    { nxt_string("Object.getOwnPropertyDescriptor(1, '0')"),
      nxt_string("undefined") },

    { nxt_string("var min = Object.getOwnPropertyDescriptor(Math, 'min').value;"
                 "[min(1,2), min(2,1), min(-1,1)]"),
      nxt_string("1,1,-1") },

    { nxt_string("Object.getOwnPropertyDescriptor()"),
      nxt_string("TypeError: cannot convert undefined argument to object") },

    { nxt_string("Object.getOwnPropertyDescriptor(undefined)"),
      nxt_string("TypeError: cannot convert undefined argument to object") },

    { nxt_string("var o = {}; o[void 0] = 'a'; Object.getOwnPropertyDescriptor(o).value"),
      nxt_string("a") },

    { nxt_string("var o = {}; o[void 0] = 'a'; Object.getOwnPropertyDescriptor(o, undefined).value"),
      nxt_string("a") },

    { nxt_string("Object.defineProperty(Object.freeze({}), 'b', {})"),
      nxt_string("TypeError: object is not extensible") },

    { nxt_string("Object.defineProperties(Object.freeze({}), {b:{}})"),
      nxt_string("TypeError: object is not extensible") },

    { nxt_string("Object.freeze()"),
      nxt_string("undefined") },

    { nxt_string("var o = Object.freeze({a:1}); o.a = 2"),
      nxt_string("TypeError: Cannot assign to read-only property 'a' of object") },

    { nxt_string("var o = Object.freeze({a:1}); delete o.a"),
      nxt_string("TypeError: Cannot delete property 'a' of object") },

    { nxt_string("var o = Object.freeze({a:1}); o.b = 1; o.b"),
      nxt_string("TypeError: Cannot add property 'b', object is not extensible") },

    { nxt_string("var o = Object.freeze(Object.create({a:1})); o.a = 2; o.a"),
      nxt_string("TypeError: Cannot add property 'a', object is not extensible") },

    { nxt_string("var o = Object.freeze({a:{b:1}}); o.a.b = 2; o.a.b"),
      nxt_string("2") },

    { nxt_string("Object.defineProperty([1,2], 'a', {value:1}).a"),
      nxt_string("1") },

    { nxt_string("var a = Object.freeze([1,2]);"
                 "Object.defineProperty(a, 'a', {value:1}).a"),
      nxt_string("TypeError: object is not extensible") },

    { nxt_string("var a = [1,2]; a.a = 1; Object.freeze(a); delete a.a"),
      nxt_string("TypeError: Cannot delete property 'a' of array") },

    { nxt_string("var a = [1,2]; a.a = 1; Object.freeze(a); a.a = 2"),
      nxt_string("TypeError: Cannot assign to read-only property 'a' of array") },

    { nxt_string("var a = Object.freeze([1,2]); a.a = 1"),
      nxt_string("TypeError: Cannot add property 'a', object is not extensible") },

    { nxt_string("Object.defineProperty(function() {}, 'a', {value:1}).a"),
      nxt_string("1") },

    { nxt_string("var f = Object.freeze(function() {});"
                 "Object.defineProperty(f, 'a', {value:1}).a"),
      nxt_string("TypeError: object is not extensible") },

    { nxt_string("var f = function() {}; f.a = 1; Object.freeze(f); delete f.a"),
      nxt_string("TypeError: Cannot delete property 'a' of function") },

    { nxt_string("var f = function() {}; f.a = 1; Object.freeze(f); f.a = 2"),
      nxt_string("TypeError: Cannot assign to read-only property 'a' of function") },

    { nxt_string("var f = Object.freeze(function() {}); f.a = 1"),
      nxt_string("TypeError: Cannot add property 'a', object is not extensible") },

    { nxt_string("Object.defineProperty(new Date(''), 'a', {value:1}).a"),
      nxt_string("1") },

    { nxt_string("var d = Object.freeze(new Date(''));"
                 "Object.defineProperty(d, 'a', {value:1}).a"),
      nxt_string("TypeError: object is not extensible") },

    { nxt_string("var d = new Date(''); d.a = 1; Object.freeze(d);"
                 "delete d.a"),
      nxt_string("TypeError: Cannot delete property 'a' of date") },

    { nxt_string("var d = new Date(''); d.a = 1; Object.freeze(d); d.a = 2"),
      nxt_string("TypeError: Cannot assign to read-only property 'a' of date") },

    { nxt_string("var d = Object.freeze(new Date('')); d.a = 1"),
      nxt_string("TypeError: Cannot add property 'a', object is not extensible") },

    { nxt_string("Object.defineProperty(new RegExp(''), 'a', {value:1}).a"),
      nxt_string("1") },

    { nxt_string("var r = Object.freeze(new RegExp(''));"
                 "Object.defineProperty(r, 'a', {value:1}).a"),
      nxt_string("TypeError: object is not extensible") },

    { nxt_string("var r = new RegExp(''); r.a = 1; Object.freeze(r); delete r.a"),
      nxt_string("TypeError: Cannot delete property 'a' of regexp") },

    { nxt_string("var r = new RegExp(''); r.a = 1; Object.freeze(r); r.a = 2"),
      nxt_string("TypeError: Cannot assign to read-only property 'a' of regexp") },

    { nxt_string("var r = Object.freeze(new RegExp('')); r.a = 1"),
      nxt_string("TypeError: Cannot add property 'a', object is not extensible") },

    { nxt_string("Object.isFrozen({a:1})"),
      nxt_string("false") },

    { nxt_string("Object.isFrozen([1,2])"),
      nxt_string("false") },

    { nxt_string("Object.isFrozen(function() {})"),
      nxt_string("false") },

    { nxt_string("Object.isFrozen(new Date(''))"),
      nxt_string("false") },

    { nxt_string("Object.isFrozen(new RegExp(''))"),
      nxt_string("false") },

    { nxt_string("Object.isFrozen()"),
      nxt_string("true") },

    { nxt_string("Object.isFrozen(1)"),
      nxt_string("true") },

    { nxt_string("Object.isFrozen('')"),
      nxt_string("true") },

    { nxt_string("Object.isFrozen(Object.defineProperties({}, {a:{value:1}}))"),
      nxt_string("false") },

    { nxt_string("var o = Object.defineProperties({}, {a:{}, b:{}});"
                 "o = Object.preventExtensions(o);"
                 "Object.isFrozen(o)"),
      nxt_string("true") },

    { nxt_string("var o = Object.defineProperties({}, {a:{}, b:{writable:1}});"
                 "o = Object.preventExtensions(o);"
                 "Object.isFrozen(o)"),
      nxt_string("false") },

    { nxt_string("var o = Object.defineProperties({}, {a:{writable:1}});"
                 "o = Object.preventExtensions(o);"
                 "Object.isFrozen(o)"),
      nxt_string("false") },

    { nxt_string("var o = Object.defineProperties({}, {a:{configurable:1}});"
                 "o = Object.preventExtensions(o);"
                 "Object.isFrozen(o)"),
      nxt_string("false") },

    { nxt_string("var o = Object.preventExtensions({a:1});"
                 "Object.isFrozen(o)"),
      nxt_string("false") },

    { nxt_string("var o = Object.freeze({a:1}); Object.isFrozen(o)"),
      nxt_string("true") },

    { nxt_string("var o = Object.seal({a:1}); o.a = 2; o.a"),
      nxt_string("2") },

    { nxt_string("var o = Object.seal({a:1}); delete o.a"),
      nxt_string("TypeError: Cannot delete property 'a' of object") },

    { nxt_string("var o = Object.seal({a:1}); o.b = 1; o.b"),
      nxt_string("TypeError: Cannot add property 'b', object is not extensible") },

    { nxt_string("var o = Object.seal(Object.create({a:1})); o.a = 2; o.a"),
      nxt_string("TypeError: Cannot add property 'a', object is not extensible") },

    { nxt_string("var o = Object.seal({a:{b:1}}); o.a.b = 2; o.a.b"),
      nxt_string("2") },

    { nxt_string("Object.seal()"),
      nxt_string("undefined") },

    { nxt_string("Object.seal(1)"),
      nxt_string("1") },

    { nxt_string("Object.seal('')"),
      nxt_string("") },

    { nxt_string("Object.isSealed({a:1})"),
      nxt_string("false") },

    { nxt_string("Object.isSealed([1,2])"),
      nxt_string("false") },

    { nxt_string("Object.isSealed(function() {})"),
      nxt_string("false") },

    { nxt_string("Object.isSealed(new Date(''))"),
      nxt_string("false") },

    { nxt_string("Object.isSealed(new RegExp(''))"),
      nxt_string("false") },

    { nxt_string("Object.isSealed()"),
      nxt_string("true") },

    { nxt_string("Object.isSealed(1)"),
      nxt_string("true") },

    { nxt_string("Object.isSealed('')"),
      nxt_string("true") },

    { nxt_string("Object.isSealed(Object.defineProperties({}, {a:{value:1}}))"),
      nxt_string("false") },

    { nxt_string("var o = Object.defineProperties({}, {a:{}, b:{}});"
                 "o = Object.preventExtensions(o);"
                 "Object.isSealed(o)"),
      nxt_string("true") },

    { nxt_string("var o = Object.defineProperties({}, {a:{}, b:{writable:1}});"
                 "o = Object.preventExtensions(o);"
                 "Object.isSealed(o)"),
      nxt_string("true") },

    { nxt_string("var o = Object.defineProperties({}, {a:{writable:1}});"
                 "o = Object.preventExtensions(o);"
                 "Object.isSealed(o)"),
      nxt_string("true") },

    { nxt_string("var o = Object.defineProperties({}, {a:{configurable:1}});"
                 "o = Object.preventExtensions(o);"
                 "Object.isSealed(o)"),
      nxt_string("false") },

    { nxt_string("var o = Object.preventExtensions({a:1});"
                 "Object.isFrozen(o)"),
      nxt_string("false") },

    { nxt_string("var o = Object.freeze({a:1}); Object.isFrozen(o)"),
      nxt_string("true") },

    { nxt_string("var o = Object.preventExtensions({a:1});"
                 "Object.defineProperty(o, 'b', {value:1})"),
      nxt_string("TypeError: object is not extensible") },

    { nxt_string("var o = Object.preventExtensions({a:1});"
                 "Object.defineProperties(o, {b:{value:1}})"),
      nxt_string("TypeError: object is not extensible") },

    { nxt_string("var o = Object.preventExtensions({a:1}); o.a = 2; o.a"),
      nxt_string("2") },

    { nxt_string("var o = Object.preventExtensions({a:1}); delete o.a; o.a"),
      nxt_string("undefined") },

    { nxt_string("var o = Object.preventExtensions({a:1}); o.b = 1; o.b"),
      nxt_string("TypeError: Cannot add property 'b', object is not extensible") },

    { nxt_string("Object.preventExtensions()"),
      nxt_string("undefined") },

    { nxt_string("Object.preventExtensions(1)"),
      nxt_string("1") },

    { nxt_string("Object.preventExtensions('')"),
      nxt_string("") },

    { nxt_string("Object.isExtensible({})"),
      nxt_string("true") },

    { nxt_string("Object.isExtensible([])"),
      nxt_string("true") },

    { nxt_string("Object.isExtensible(function() {})"),
      nxt_string("true") },

    { nxt_string("Object.isExtensible(new Date(''))"),
      nxt_string("true") },

    { nxt_string("Object.isExtensible(new RegExp(''))"),
      nxt_string("true") },

    { nxt_string("Object.isExtensible()"),
      nxt_string("false") },

    { nxt_string("Object.isExtensible(1)"),
      nxt_string("false") },

    { nxt_string("Object.isExtensible('')"),
      nxt_string("false") },

    { nxt_string("Object.isExtensible(Object.preventExtensions({}))"),
      nxt_string("false") },

    { nxt_string("Object.isExtensible(Object.preventExtensions([]))"),
      nxt_string("false") },

    { nxt_string("Object.isExtensible(Object.freeze({}))"),
      nxt_string("false") },

    { nxt_string("Object.isExtensible(Object.freeze([]))"),
      nxt_string("false") },

    { nxt_string("var d = new Date(''); d +' '+ d.getTime()"),
      nxt_string("Invalid Date NaN") },

    { nxt_string("var d = new Date(1); d = d + ''; d.slice(0, 33)"),
      nxt_string("Thu Jan 01 1970 00:00:00 GMT+0000") },

    { nxt_string("var d = new Date(1308895200000); d.getTime()"),
      nxt_string("1308895200000") },

    { nxt_string("var d = new Date(2011, 5, 24, 18, 45); d.getTime()"),
      nxt_string("1308941100000") },

    { nxt_string("var d = new Date(2011, 5, 24, 18, 45); d.valueOf()"),
      nxt_string("1308941100000") },

    { nxt_string("var d = new Date(2011, 5, 24, 18, 45);"
                 "d.toString().slice(0, 33)"),
      nxt_string("Fri Jun 24 2011 18:45:00 GMT+0000") },

    { nxt_string("var d = new Date(2011, 5, 24, 18, 45); d.toDateString()"),
      nxt_string("Fri Jun 24 2011") },

    { nxt_string("var d = new Date(2011, 5, 24, 18, 45);"
                 "d.toTimeString().slice(0, 17)"),
      nxt_string("18:45:00 GMT+0000") },

    { nxt_string("var d = new Date(2011, 5, 24, 18, 45); d.toUTCString()"),
      nxt_string("Fri Jun 24 2011 18:45:00 GMT") },

    { nxt_string("var d = new Date(2011, 5, 24, 18, 45, 12, 625);"
                 "d.toISOString()"),
      nxt_string("2011-06-24T18:45:12.625Z") },

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

    { nxt_string("Date.parse('2011')"),
      nxt_string("1293840000000") },

    { nxt_string("Date.parse('+002011')"),
      nxt_string("1293840000000") },

    { nxt_string("Date.parse('2011-06')"),
      nxt_string("1306886400000") },

    { nxt_string("Date.parse('2011-06-24')"),
      nxt_string("1308873600000") },

    { nxt_string("Date.parse('2011-06-24T06')"),
      nxt_string("NaN") },

    { nxt_string("Date.parse('2011-06-24T06:')"),
      nxt_string("NaN") },

    { nxt_string("Date.parse('2011-06-24T06:01:')"),
      nxt_string("NaN") },

    { nxt_string("Date.parse('2011-06-24T06:01Z')"),
      nxt_string("1308895260000") },

    { nxt_string("Date.parse('2011-06-24T06:01:02:')"),
      nxt_string("NaN") },

    { nxt_string("Date.parse('2011-06-24T06:01:02Z')"),
      nxt_string("1308895262000") },

    { nxt_string("Date.parse('2011-06-24T06:01:02.Z')"),
      nxt_string("NaN") },

    { nxt_string("Date.parse('2011-06-24T06:01:02.6Z')"),
      nxt_string("1308895262600") },

    { nxt_string("Date.parse('2011-06-24T06:01:02.62Z')"),
      nxt_string("1308895262620") },

    { nxt_string("Date.parse('2011-06-24T06:01:02:625Z')"),
      nxt_string("NaN") },

    { nxt_string("Date.parse('2011-06-24T06:01:02.625Z')"),
      nxt_string("1308895262625") },

    { nxt_string("Date.parse('2011-06-24T06:01:02.6255555Z')"),
      nxt_string("1308895262625") },

    { nxt_string("Date.parse('2011-06-24T06:01:02.625555Z5')"),
      nxt_string("NaN") },

    { nxt_string("var d = new Date(); var str = d.toISOString();"
                 "var diff = Date.parse(str) - Date.parse(str.substring(0, str.length - 1));"
                 "d.getTimezoneOffset() == -diff/1000/60"),
      nxt_string("true") },

    { nxt_string("Date.parse('24 Jun 2011')"),
      nxt_string("1308873600000") },

    { nxt_string("Date.parse('Fri, 24 Jun 2011 18:48')"),
      nxt_string("1308941280000") },

    { nxt_string("Date.parse('Fri, 24 Jun 2011 18:48:02')"),
      nxt_string("1308941282000") },

    { nxt_string("Date.parse('Fri, 24 Jun 2011 18:48:02 GMT')"),
      nxt_string("1308941282000") },

    { nxt_string("Date.parse('Fri, 24 Jun 2011 18:48:02 +1245')"),
      nxt_string("1308895382000") },

    { nxt_string("Date.parse('Jun 24 2011')"),
      nxt_string("1308873600000") },

    { nxt_string("Date.parse('Fri Jun 24 2011 18:48')"),
      nxt_string("1308941280000") },

    { nxt_string("Date.parse('Fri Jun 24 2011 18:48:02')"),
      nxt_string("1308941282000") },

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
      nxt_string("18") },

    { nxt_string("var d = new Date(2011, 5, 24, 18, 45); d.getMinutes()"),
      nxt_string("45") },

    { nxt_string("var d = new Date(2011, 5, 24, 18, 45); d.getUTCMinutes()"),
      nxt_string("45") },

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
      nxt_string("0") },

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
      nxt_string("1308895387003") },

    { nxt_string("var d = new Date(1308895323625); d.setMinutes(3, 2);"
                 "d.getTime()"),
      nxt_string("1308895382625") },

    { nxt_string("var d = new Date(1308895323625); d.setMinutes(3);"
                 "d.getTime()"),
      nxt_string("1308895383625") },

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
      nxt_string("1308945787003") },

    { nxt_string("var d = new Date(1308895323625); d.setHours(20, 3, 2);"
                 "d.getTime()"),
      nxt_string("1308945782625") },

    { nxt_string("var d = new Date(1308895323625); d.setHours(20, 3);"
                 "d.getTime()"),
      nxt_string("1308945783625") },

    { nxt_string("var d = new Date(1308895323625); d.setHours(20);"
                 "d.getTime()"),
      nxt_string("1308945723625") },

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
      nxt_string("1299736923625") },

    { nxt_string("var d = new Date(1308895323625); d.setUTCMonth(2, 10);"
                 "d.getTime()"),
      nxt_string("1299736923625") },

    { nxt_string("var d = new Date(1308895323625); d.setMonth(2);"
                 "d.getTime()"),
      nxt_string("1300946523625") },

    { nxt_string("var d = new Date(1308895323625); d.setUTCMonth(2);"
                 "d.getTime()"),
      nxt_string("1300946523625") },

    { nxt_string("var d = new Date(1308895323625); d.setFullYear(2010, 2, 10);"
                 "d.getTime()"),
      nxt_string("1268200923625") },

    { nxt_string("var d = new Date(1308895323625);"
                 "d.setUTCFullYear(2010, 2, 10); d.getTime()"),
      nxt_string("1268200923625") },

    { nxt_string("var d = new Date(1308895323625); d.setFullYear(2010, 2);"
                 "d.getTime()"),
      nxt_string("1269410523625") },

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
      nxt_string("2011-06-24T18:45:12.625Z") },

    { nxt_string("var o = { toISOString: function() { return 'OK' } };"
                 "Date.prototype.toJSON.call(o, 1)"),
      nxt_string("OK") },

    { nxt_string("var d = new Date; d.__proto__"),
      nxt_string("Invalid Date") },

    { nxt_string("var d = new Date(); d.__proto__"),
      nxt_string("Invalid Date") },

    { nxt_string("var d = new Date(); d.__proto__ === Date.prototype"),
      nxt_string("true") },

    { nxt_string("new Date(NaN)"),
      nxt_string("Invalid Date") },

    { nxt_string("[0].map(new Date().getDate)"),
      nxt_string("TypeError: cannot convert undefined to date") },

    { nxt_string("new Date(eval)"),
      nxt_string("Invalid Date") },

    { nxt_string("Date.UTC(eval)"),
      nxt_string("NaN") },

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

    { nxt_string("Date.prototype"),
      nxt_string("Invalid Date") },

    { nxt_string("Date.prototype.valueOf()"),
      nxt_string("NaN") },

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
      nxt_string("InternalError: Not implemented") },

    /* Math. */

    { nxt_string("Math.PI"),
      nxt_string("3.141592653589793") },

    { nxt_string("Math.abs()"),
      nxt_string("NaN") },

    { nxt_string("Math.abs(5)"),
      nxt_string("5") },

    { nxt_string("Math.abs(-5)"),
      nxt_string("5") },

    { nxt_string("Math.abs('5.0')"),
      nxt_string("5") },

    { nxt_string("Math.abs('abc')"),
      nxt_string("NaN") },

    { nxt_string("Math.acos()"),
      nxt_string("NaN") },

    { nxt_string("Math.acos(NaN)"),
      nxt_string("NaN") },

    { nxt_string("Math.acos('abc')"),
      nxt_string("NaN") },

    { nxt_string("Math.acos(1.1)"),
      nxt_string("NaN") },

    { nxt_string("Math.acos(-1.1)"),
      nxt_string("NaN") },

    { nxt_string("Math.acos('1')"),
      nxt_string("0") },

    { nxt_string("Math.acos(0) - Math.PI/2"),
      nxt_string("0") },

    { nxt_string("Math.acosh()"),
      nxt_string("NaN") },

    { nxt_string("Math.acosh('abc')"),
      nxt_string("NaN") },

    { nxt_string("Math.acosh(0.9)"),
      nxt_string("NaN") },

    { nxt_string("Math.acosh(1)"),
      nxt_string("0") },

    { nxt_string("Math.acosh('Infinity')"),
      nxt_string("Infinity") },

    /*
     * The difference is Number.EPSILON on Linux/i686
     * and zero on other platforms.
     */
    { nxt_string("Math.abs(Math.acosh((1/Math.E + Math.E)/2) - 1)"
                 " <= Number.EPSILON"),
      nxt_string("true") },

    { nxt_string("Math.asin()"),
      nxt_string("NaN") },

    { nxt_string("Math.asin(NaN)"),
      nxt_string("NaN") },

    { nxt_string("Math.asin('abc')"),
      nxt_string("NaN") },

    { nxt_string("Math.asin(1.1)"),
      nxt_string("NaN") },

    { nxt_string("Math.asin(-1.1)"),
      nxt_string("NaN") },

    { nxt_string("Math.asin(0)"),
      nxt_string("0") },

    { nxt_string("Math.asin('-0')"),
      nxt_string("-0") },

    { nxt_string("Math.asin(1) - Math.PI/2"),
      nxt_string("0") },

    { nxt_string("Math.asinh()"),
      nxt_string("NaN") },

    { nxt_string("Math.asinh('abc')"),
      nxt_string("NaN") },

    { nxt_string("Math.asinh(0)"),
      nxt_string("0") },

    { nxt_string("Math.asinh('-0')"),
      nxt_string("-0") },

    { nxt_string("Math.asinh(Infinity)"),
      nxt_string("Infinity") },

    { nxt_string("Math.asinh(-Infinity)"),
      nxt_string("-Infinity") },

    { nxt_string("Math.asinh((Math.E - 1/Math.E)/2)"),
      nxt_string("1") },

    { nxt_string("Math.atan()"),
      nxt_string("NaN") },

    { nxt_string("Math.atan(NaN)"),
      nxt_string("NaN") },

    { nxt_string("Math.atan('abc')"),
      nxt_string("NaN") },

    { nxt_string("Math.atan('Infinity') - Math.PI/2"),
      nxt_string("0") },

    { nxt_string("Math.atan(-Infinity) + Math.PI/2"),
      nxt_string("0") },

    { nxt_string("Math.atan(0)"),
      nxt_string("0") },

    { nxt_string("Math.atan('-0')"),
      nxt_string("-0") },

    { nxt_string("Math.atan(1) - Math.PI/4"),
      nxt_string("0") },

    { nxt_string("Math.atan2()"),
      nxt_string("NaN") },

    { nxt_string("Math.atan2(1)"),
      nxt_string("NaN") },

    { nxt_string("Math.atan2('abc', 1)"),
      nxt_string("NaN") },

    { nxt_string("Math.atan2(1, 0) - Math.PI/2"),
      nxt_string("0") },

    { nxt_string("Math.atan2('1', -0) - Math.PI/2"),
      nxt_string("0") },

    { nxt_string("Math.atan2(0, '1')"),
      nxt_string("0") },

    { nxt_string("Math.atan2(0, 0)"),
      nxt_string("0") },

    { nxt_string("Math.atan2(0, -0) - Math.PI"),
      nxt_string("0") },

    { nxt_string("Math.atan2('0', -1) - Math.PI"),
      nxt_string("0") },

    { nxt_string("Math.atan2(-0, '0.1')"),
      nxt_string("-0") },

    { nxt_string("Math.atan2(-0, 0)"),
      nxt_string("-0") },

    { nxt_string("Math.atan2(-0, -0) + Math.PI"),
      nxt_string("0") },

    { nxt_string("Math.atan2('-0', '-1') + Math.PI"),
      nxt_string("0") },

    { nxt_string("Math.atan2(-0.1, 0) + Math.PI/2"),
      nxt_string("0") },

    { nxt_string("Math.atan2(-1, -0) + Math.PI/2"),
      nxt_string("0") },

    { nxt_string("Math.atan2(1, 'Infinity')"),
      nxt_string("0") },

    { nxt_string("Math.atan2(0.1, -Infinity) - Math.PI"),
      nxt_string("0") },

    { nxt_string("Math.atan2(-1, Infinity)"),
      nxt_string("-0") },

    { nxt_string("Math.atan2('-0.1', -Infinity) + Math.PI"),
      nxt_string("0") },

    { nxt_string("Math.atan2(Infinity, -5) - Math.PI/2"),
      nxt_string("0") },

    { nxt_string("Math.atan2(-Infinity, 5) + Math.PI/2"),
      nxt_string("0") },

    { nxt_string("Math.atan2('Infinity', 'Infinity') - Math.PI/4"),
      nxt_string("0") },

    { nxt_string("Math.atan2(Infinity, -Infinity) - 3*Math.PI/4"),
      nxt_string("0") },

    { nxt_string("Math.atan2(-Infinity, 'Infinity') + Math.PI/4"),
      nxt_string("0") },

    { nxt_string("Math.atan2('-Infinity', -Infinity) + 3*Math.PI/4"),
      nxt_string("0") },

    { nxt_string("Math.atan2(1, 1) - Math.atan2(-5, -5) - Math.PI"),
      nxt_string("0") },

    { nxt_string("Math.atanh()"),
      nxt_string("NaN") },

    { nxt_string("Math.atanh('abc')"),
      nxt_string("NaN") },

    { nxt_string("Math.atanh(-1.1)"),
      nxt_string("NaN") },

    { nxt_string("Math.atanh(1.1)"),
      nxt_string("NaN") },

    { nxt_string("Math.atanh(1)"),
      nxt_string("Infinity") },

    { nxt_string("Math.atanh('-1')"),
      nxt_string("-Infinity") },

    { nxt_string("Math.atanh(0)"),
      nxt_string("0") },

    { nxt_string("Math.atanh(-0)"),
      nxt_string("-0") },

    /*
     * The difference is Number.EPSILON on Linux/i686
     * and zero on other platforms.
     */
    { nxt_string("Math.abs(1 - Math.atanh((Math.E - 1)/(Math.E + 1)) * 2)"
                 " <= Number.EPSILON"),
      nxt_string("true") },

    { nxt_string("Math.cbrt()"),
      nxt_string("NaN") },

    { nxt_string("Math.cbrt('abc')"),
      nxt_string("NaN") },

    { nxt_string("Math.cbrt(0)"),
      nxt_string("0") },

    { nxt_string("Math.cbrt('-0')"),
      nxt_string("-0") },

    { nxt_string("Math.cbrt(Infinity)"),
      nxt_string("Infinity") },

    { nxt_string("Math.cbrt(-Infinity)"),
      nxt_string("-Infinity") },

    { nxt_string("(Math.cbrt('27') - 3) < 1e-15"),
      nxt_string("true") },

    { nxt_string("Math.cbrt(-1)"),
      nxt_string("-1") },

    { nxt_string("Math.ceil()"),
      nxt_string("NaN") },

    { nxt_string("Math.ceil('abc')"),
      nxt_string("NaN") },

    { nxt_string("Math.ceil(0)"),
      nxt_string("0") },

    { nxt_string("Math.ceil('-0')"),
      nxt_string("-0") },

    { nxt_string("Math.ceil('Infinity')"),
      nxt_string("Infinity") },

    { nxt_string("Math.ceil(-Infinity)"),
      nxt_string("-Infinity") },

    { nxt_string("Math.ceil(-0.9)"),
      nxt_string("-0") },

    { nxt_string("Math.ceil(3.1)"),
      nxt_string("4") },

    { nxt_string("Math.clz32()"),
      nxt_string("32") },

    { nxt_string("Math.clz32('abc')"),
      nxt_string("32") },

    { nxt_string("Math.clz32(NaN)"),
      nxt_string("32") },

    { nxt_string("Math.clz32(Infinity)"),
      nxt_string("32") },

    { nxt_string("Math.clz32('1')"),
      nxt_string("31") },

    { nxt_string("Math.clz32(0)"),
      nxt_string("32") },

    { nxt_string("Math.clz32('65535')"),
      nxt_string("16") },

    { nxt_string("Math.clz32(-1)"),
      nxt_string("0") },

    { nxt_string("Math.clz32(4294967298)"),
      nxt_string("30") },

    { nxt_string("Math.cos()"),
      nxt_string("NaN") },

    { nxt_string("Math.cos('abc')"),
      nxt_string("NaN") },

    { nxt_string("Math.cos('0')"),
      nxt_string("1") },

    { nxt_string("Math.cos(-0)"),
      nxt_string("1") },

    { nxt_string("Math.cos(Infinity)"),
      nxt_string("NaN") },

    { nxt_string("Math.cos(-Infinity)"),
      nxt_string("NaN") },

    { nxt_string("Math.cos(Math.PI*2)"),
      nxt_string("1") },

    { nxt_string("Math.cosh()"),
      nxt_string("NaN") },

    { nxt_string("Math.cosh('abc')"),
      nxt_string("NaN") },

    { nxt_string("Math.cosh('0')"),
      nxt_string("1") },

    { nxt_string("Math.cosh(-0)"),
      nxt_string("1") },

    { nxt_string("Math.cosh(Infinity)"),
      nxt_string("Infinity") },

    { nxt_string("Math.cosh(-Infinity)"),
      nxt_string("Infinity") },

    /*
     * The difference is Number.EPSILON on FreeBSD
     * and zero on other platforms.
     */
    { nxt_string("Math.abs(Math.cosh(1) - (1/Math.E + Math.E)/2)"
                 " <= Number.EPSILON"),
      nxt_string("true") },

    { nxt_string("Math.exp()"),
      nxt_string("NaN") },

    { nxt_string("Math.exp('abc')"),
      nxt_string("NaN") },

    { nxt_string("Math.exp('0')"),
      nxt_string("1") },

    { nxt_string("Math.exp(-0)"),
      nxt_string("1") },

    { nxt_string("Math.exp(Infinity)"),
      nxt_string("Infinity") },

    { nxt_string("Math.exp(-Infinity)"),
      nxt_string("0") },

    /*
     * The difference is 2 * Number.EPSILON on FreeBSD
     * and zero on other platforms.
     */
    { nxt_string("Math.abs(Math.exp(1) - Math.E) <= 2 * Number.EPSILON"),
      nxt_string("true") },

    { nxt_string("Math.expm1()"),
      nxt_string("NaN") },

    { nxt_string("Math.expm1('abc')"),
      nxt_string("NaN") },

    { nxt_string("Math.expm1('0')"),
      nxt_string("0") },

    { nxt_string("Math.expm1(-0)"),
      nxt_string("-0") },

    { nxt_string("Math.expm1(Infinity)"),
      nxt_string("Infinity") },

    { nxt_string("Math.expm1(-Infinity)"),
      nxt_string("-1") },

    /*
     * The difference is 2 * Number.EPSILON on FreeBSD, Solaris,
     * and MacOSX and zero on other platforms.
     */
    { nxt_string("Math.abs(1 + Math.expm1(1) - Math.E) <= 2 * Number.EPSILON"),
      nxt_string("true") },

    { nxt_string("Math.floor()"),
      nxt_string("NaN") },

    { nxt_string("Math.floor('abc')"),
      nxt_string("NaN") },

    { nxt_string("Math.floor(0)"),
      nxt_string("0") },

    { nxt_string("Math.floor('-0')"),
      nxt_string("-0") },

    { nxt_string("Math.floor('Infinity')"),
      nxt_string("Infinity") },

    { nxt_string("Math.floor(-Infinity)"),
      nxt_string("-Infinity") },

    { nxt_string("Math.floor(0.9)"),
      nxt_string("0") },

    { nxt_string("Math.floor(-3.1)"),
      nxt_string("-4") },

    { nxt_string("Math.fround()"),
      nxt_string("NaN") },

    { nxt_string("Math.fround('abc')"),
      nxt_string("NaN") },

    { nxt_string("Math.fround(0)"),
      nxt_string("0") },

    { nxt_string("Math.fround('-0')"),
      nxt_string("-0") },

    { nxt_string("Math.fround('Infinity')"),
      nxt_string("Infinity") },

    { nxt_string("Math.fround(-Infinity)"),
      nxt_string("-Infinity") },

    { nxt_string("Math.fround('-1.5')"),
      nxt_string("-1.5") },

    { nxt_string("Math.fround(16777216)"),
      nxt_string("16777216") },

    { nxt_string("Math.fround(-16777217)"),
      nxt_string("-16777216") },

    { nxt_string("Math.hypot()"),
      nxt_string("0") },

    { nxt_string("Math.hypot(1, 2, 'abc')"),
      nxt_string("NaN") },

    { nxt_string("Math.hypot(1, NaN, 3)"),
      nxt_string("NaN") },

    { nxt_string("Math.hypot(1, NaN, -Infinity)"),
      nxt_string("Infinity") },

    { nxt_string("Math.hypot(-42)"),
      nxt_string("42") },

    { nxt_string("Math.hypot(8, -15)"),
      nxt_string("17") },

    { nxt_string("Math.hypot(3, -4, 12.0, '84', 132)"),
      nxt_string("157") },

    { nxt_string("Math.imul()"),
      nxt_string("0") },

    { nxt_string("Math.imul(1)"),
      nxt_string("0") },

    { nxt_string("Math.imul('a', 1)"),
      nxt_string("0") },

    { nxt_string("Math.imul(1, NaN)"),
      nxt_string("0") },

    { nxt_string("Math.imul(2, '3')"),
      nxt_string("6") },

    { nxt_string("Math.imul('3.9', -2.1)"),
      nxt_string("-6") },

    { nxt_string("Math.imul(2, 2147483647)"),
      nxt_string("-2") },

    { nxt_string("Math.imul(Number.MAX_SAFE_INTEGER, 2)"),
      nxt_string("-2") },

    { nxt_string("Math.imul(1, Number.MAX_SAFE_INTEGER + 1)"),
      nxt_string("0") },

    { nxt_string("Math.imul(2, Number.MIN_SAFE_INTEGER)"),
      nxt_string("2") },

    { nxt_string("Math.imul(Number.MIN_SAFE_INTEGER - 1, 1)"),
      nxt_string("0") },

    { nxt_string("Math.imul(2, 4294967297)"),
      nxt_string("2") },

    { nxt_string("Math.imul(-4294967297, 4294967297)"),
      nxt_string("-1") },

    { nxt_string("Math.imul(4294967297, -4294967298)"),
      nxt_string("-2") },

    { nxt_string("Math.imul(-4294967290, 4294967290)"),
      nxt_string("-36") },

    { nxt_string("Math.imul(-Infinity, 1)"),
      nxt_string("0") },

    { nxt_string("Math.imul(1, Infinity)"),
      nxt_string("0") },

    { nxt_string("Math.imul(Number.MAX_VALUE, 1)"),
      nxt_string("0") },

    { nxt_string("Math.imul(1, -Number.MAX_VALUE)"),
      nxt_string("0") },

    { nxt_string("Math.log()"),
      nxt_string("NaN") },

    { nxt_string("Math.log('abc')"),
      nxt_string("NaN") },

    { nxt_string("Math.log(-1)"),
      nxt_string("NaN") },

    { nxt_string("Math.log(0)"),
      nxt_string("-Infinity") },

    { nxt_string("Math.log('-0')"),
      nxt_string("-Infinity") },

    { nxt_string("Math.log(1)"),
      nxt_string("0") },

    { nxt_string("Math.log(Infinity)"),
      nxt_string("Infinity") },

    { nxt_string("Math.log(Math.E)"),
      nxt_string("1") },

    { nxt_string("Math.log10()"),
      nxt_string("NaN") },

    { nxt_string("Math.log10('abc')"),
      nxt_string("NaN") },

    { nxt_string("Math.log10(-1)"),
      nxt_string("NaN") },

    { nxt_string("Math.log10(0)"),
      nxt_string("-Infinity") },

    { nxt_string("Math.log10('-0')"),
      nxt_string("-Infinity") },

    { nxt_string("Math.log10(1)"),
      nxt_string("0") },

    { nxt_string("Math.log10(Infinity)"),
      nxt_string("Infinity") },

    { nxt_string("Math.log10(1000)"),
      nxt_string("3") },

    { nxt_string("Math.log1p()"),
      nxt_string("NaN") },

    { nxt_string("Math.log1p('abc')"),
      nxt_string("NaN") },

    { nxt_string("Math.log1p(-2)"),
      nxt_string("NaN") },

    { nxt_string("Math.log1p('-1')"),
      nxt_string("-Infinity") },

    { nxt_string("Math.log1p(0)"),
      nxt_string("0") },

    { nxt_string("Math.log1p(-0)"),
      nxt_string("-0") },

    { nxt_string("Math.log1p(Infinity)"),
      nxt_string("Infinity") },

    { nxt_string("Math.log1p(Math.E - 1)"),
      nxt_string("1") },

    { nxt_string("Math.log2()"),
      nxt_string("NaN") },

    { nxt_string("Math.log2('abc')"),
      nxt_string("NaN") },

    { nxt_string("Math.log2(-1)"),
      nxt_string("NaN") },

    { nxt_string("Math.log2(0)"),
      nxt_string("-Infinity") },

    { nxt_string("Math.log2('-0')"),
      nxt_string("-Infinity") },

    { nxt_string("Math.log2(1)"),
      nxt_string("0") },

    { nxt_string("Math.log2(Infinity)"),
      nxt_string("Infinity") },

    { nxt_string("Math.log2(128)"),
      nxt_string("7") },

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

    { nxt_string("Math.pow(1, NaN)"),
      nxt_string("NaN") },

    { nxt_string("Math.pow(3, NaN)"),
      nxt_string("NaN") },

    { nxt_string("Math.pow('a', -0)"),
      nxt_string("1") },

    { nxt_string("Math.pow(1.1, Infinity)"),
      nxt_string("Infinity") },

    { nxt_string("Math.pow(-1.1, -Infinity)"),
      nxt_string("0") },

    { nxt_string("Math.pow(-1, Infinity)"),
      nxt_string("NaN") },

    { nxt_string("Math.pow(1, -Infinity)"),
      nxt_string("NaN") },

    { nxt_string("Math.pow(-0.9, Infinity)"),
      nxt_string("0") },

    { nxt_string("Math.pow(0.9, -Infinity)"),
      nxt_string("Infinity") },

    { nxt_string("Math.pow('Infinity', 0.1)"),
      nxt_string("Infinity") },

    { nxt_string("Math.pow(Infinity, '-0.1')"),
      nxt_string("0") },

    { nxt_string("Math.pow(-Infinity, 3)"),
      nxt_string("-Infinity") },

    { nxt_string("Math.pow('-Infinity', '3.1')"),
      nxt_string("Infinity") },

    { nxt_string("Math.pow(-Infinity, '-3')"),
      nxt_string("-0") },

    { nxt_string("Math.pow('-Infinity', -2)"),
      nxt_string("0") },

    { nxt_string("Math.pow('0', 0.1)"),
      nxt_string("0") },

#ifndef __NetBSD__  /* NetBSD 7: pow(0, negative) == -Infinity. */
    { nxt_string("Math.pow(0, '-0.1')"),
      nxt_string("Infinity") },
#endif

    { nxt_string("Math.pow(-0, 3)"),
      nxt_string("-0") },

    { nxt_string("Math.pow('-0', '3.1')"),
      nxt_string("0") },

    { nxt_string("Math.pow(-0, '-3')"),
      nxt_string("-Infinity") },

#ifndef __NetBSD__  /* NetBSD 7: pow(0, negative) == -Infinity. */
    { nxt_string("Math.pow('-0', -2)"),
      nxt_string("Infinity") },
#endif

    { nxt_string("Math.pow(-3, 0.1)"),
      nxt_string("NaN") },

    { nxt_string("var a = Math.random(); a >= 0 && a < 1"),
      nxt_string("true") },

    { nxt_string("Math.round()"),
      nxt_string("NaN") },

    { nxt_string("Math.round('abc')"),
      nxt_string("NaN") },

    { nxt_string("Math.round(0)"),
      nxt_string("0") },

    { nxt_string("Math.round('-0')"),
      nxt_string("-0") },

    { nxt_string("Math.round('Infinity')"),
      nxt_string("Infinity") },

    { nxt_string("Math.round(-Infinity)"),
      nxt_string("-Infinity") },

    { nxt_string("Math.round(0.4)"),
      nxt_string("0") },

    { nxt_string("Math.round('0.5')"),
      nxt_string("1") },

    { nxt_string("Math.round('-0.4')"),
      nxt_string("-0") },

    { nxt_string("Math.round(-0.5)"),
      nxt_string("-1") },

    { nxt_string("Math.sign(5)"),
      nxt_string("1") },

    { nxt_string("Math.sign(-5)"),
      nxt_string("-1") },

    { nxt_string("Math.sign(0)"),
      nxt_string("0") },

    { nxt_string("Math.sign(-0.0)"),
      nxt_string("-0") },

    { nxt_string("Math.sign(NaN)"),
      nxt_string("NaN") },

    { nxt_string("Math.sign()"),
      nxt_string("NaN") },

    { nxt_string("Math.sin()"),
      nxt_string("NaN") },

    { nxt_string("Math.sin('abc')"),
      nxt_string("NaN") },

    { nxt_string("Math.sin('0')"),
      nxt_string("0") },

    { nxt_string("Math.sin(-0)"),
      nxt_string("-0") },

    { nxt_string("Math.sin(Infinity)"),
      nxt_string("NaN") },

    { nxt_string("Math.sin(-Infinity)"),
      nxt_string("NaN") },

    { nxt_string("Math.sin(-Math.PI/2)"),
      nxt_string("-1") },

    { nxt_string("Math.sinh()"),
      nxt_string("NaN") },

    { nxt_string("Math.sinh('abc')"),
      nxt_string("NaN") },

    { nxt_string("Math.sinh('0')"),
      nxt_string("0") },

    { nxt_string("Math.sinh(-0)"),
      nxt_string("-0") },

    { nxt_string("Math.sinh(Infinity)"),
      nxt_string("Infinity") },

    { nxt_string("Math.sinh(-Infinity)"),
      nxt_string("-Infinity") },

    /*
     * The difference is Number.EPSILON on Solaris
     * and zero on other platforms.
     */
    { nxt_string("Math.abs(Math.sinh(1) - (Math.E - 1/Math.E)/2)"
                 " <= Number.EPSILON"),
      nxt_string("true") },

    { nxt_string("Math.sqrt()"),
      nxt_string("NaN") },

    { nxt_string("Math.sqrt('abc')"),
      nxt_string("NaN") },

    { nxt_string("Math.sqrt('0')"),
      nxt_string("0") },

    { nxt_string("Math.sqrt(-0)"),
      nxt_string("-0") },

    { nxt_string("Math.sqrt(Infinity)"),
      nxt_string("Infinity") },

    { nxt_string("Math.sqrt(-0.1)"),
      nxt_string("NaN") },

    { nxt_string("Math.sqrt('9.0')"),
      nxt_string("3") },

    { nxt_string("Math.tan()"),
      nxt_string("NaN") },

    { nxt_string("Math.tan('abc')"),
      nxt_string("NaN") },

    { nxt_string("Math.tan('0')"),
      nxt_string("0") },

    { nxt_string("Math.tan(-0)"),
      nxt_string("-0") },

    { nxt_string("Math.tan(Infinity)"),
      nxt_string("NaN") },

    { nxt_string("Math.tan(-Infinity)"),
      nxt_string("NaN") },

    { nxt_string("Math.tan(Math.PI/3) + Math.tan(-Math.PI/3)"),
      nxt_string("0") },

    { nxt_string("Math.tanh()"),
      nxt_string("NaN") },

    { nxt_string("Math.tanh('abc')"),
      nxt_string("NaN") },

    { nxt_string("Math.tanh('0')"),
      nxt_string("0") },

    { nxt_string("Math.tanh(-0)"),
      nxt_string("-0") },

    { nxt_string("Math.tanh(Infinity)"),
      nxt_string("1") },

    { nxt_string("Math.tanh(-Infinity)"),
      nxt_string("-1") },

    { nxt_string("Math.tanh(0.5) - (Math.E - 1)/(Math.E + 1)"),
      nxt_string("0") },

    { nxt_string("Math.trunc(3.9)"),
      nxt_string("3") },

    { nxt_string("Math.trunc(-3.9)"),
      nxt_string("-3") },

    { nxt_string("Math.trunc(0)"),
      nxt_string("0") },

    { nxt_string("Math.trunc(-0)"),
      nxt_string("-0") },

    { nxt_string("Math.trunc(0.9)"),
      nxt_string("0") },

    { nxt_string("Math.trunc(-0.9)"),
      nxt_string("-0") },

    { nxt_string("Math.trunc(Infinity)"),
      nxt_string("Infinity") },

    { nxt_string("Math.trunc(-Infinity)"),
      nxt_string("-Infinity") },

    { nxt_string("Math.trunc(NaN)"),
      nxt_string("NaN") },

    { nxt_string("Math.trunc()"),
      nxt_string("NaN") },

    /* ES5FIX: "[object Math]". */

    { nxt_string("Math"),
      nxt_string("[object Object]") },

    { nxt_string("Math.x = function (x) {return 2*x;}; Math.x(3)"),
      nxt_string("6") },

    { nxt_string("isNaN"),
      nxt_string("[object Function]") },

    { nxt_string("isNaN()"),
      nxt_string("true") },

    { nxt_string("isNaN(123)"),
      nxt_string("false") },

    { nxt_string("isNaN('123')"),
      nxt_string("false") },

    { nxt_string("isNaN('Infinity')"),
      nxt_string("false") },

    { nxt_string("isNaN('abc')"),
      nxt_string("true") },

    { nxt_string("isFinite"),
      nxt_string("[object Function]") },

    { nxt_string("isFinite()"),
      nxt_string("false") },

    { nxt_string("isFinite(123)"),
      nxt_string("true") },

    { nxt_string("isFinite('123')"),
      nxt_string("true") },

    { nxt_string("isFinite('Infinity')"),
      nxt_string("false") },

    { nxt_string("isFinite('abc')"),
      nxt_string("false") },

    { nxt_string("parseInt('12345abc')"),
      nxt_string("12345") },

    { nxt_string("parseInt('123', 0)"),
      nxt_string("123") },

    { nxt_string("parseInt('0XaBc', 0)"),
      nxt_string("2748") },

    { nxt_string("parseInt(' 123')"),
      nxt_string("123") },

    { nxt_string("parseInt('1010', 2)"),
      nxt_string("10") },

    { nxt_string("parseInt('aBc', 16)"),
      nxt_string("2748") },

    { nxt_string("parseInt('0XaBc')"),
      nxt_string("2748") },

    { nxt_string("parseInt('-0xabc')"),
      nxt_string("-2748") },

    { nxt_string("parseInt('njscript', 36)"),
      nxt_string("1845449130881") },

    { nxt_string("parseInt('0x')"),
      nxt_string("NaN") },

    { nxt_string("parseInt('z')"),
      nxt_string("NaN") },

    { nxt_string("parseInt('0xz')"),
      nxt_string("NaN") },

    { nxt_string("parseInt('0x', 16)"),
      nxt_string("NaN") },

    { nxt_string("parseInt('0x', 33)"),
      nxt_string("0") },

    { nxt_string("parseInt('0x', 34)"),
      nxt_string("33") },

    { nxt_string("parseInt('0', 1)"),
      nxt_string("NaN") },

    { nxt_string("parseInt('0', 37)"),
      nxt_string("NaN") },

    { nxt_string("parseFloat('12345abc')"),
      nxt_string("12345") },

    { nxt_string("parseFloat('0x')"),
      nxt_string("0") },

    { nxt_string("parseFloat('0xff')"),
      nxt_string("0") },

    { nxt_string("parseFloat('Infinity')"),
      nxt_string("Infinity") },

    { nxt_string("parseFloat(' Infinityzz')"),
      nxt_string("Infinity") },

    { nxt_string("parseFloat('Infinit')"),
      nxt_string("NaN") },

    { nxt_string("parseFloat('5.7e1')"),
      nxt_string("57") },

    { nxt_string("parseFloat('-5.7e-1')"),
      nxt_string("-0.57") },

    { nxt_string("parseFloat('-5.e-1')"),
      nxt_string("-0.5") },

    { nxt_string("parseFloat('5.7e+01')"),
      nxt_string("57") },

    { nxt_string("parseFloat(' 5.7e+01abc')"),
      nxt_string("57") },

    { nxt_string("parseFloat('-5.7e-1abc')"),
      nxt_string("-0.57") },

    { nxt_string("parseFloat('-5.7e')"),
      nxt_string("-5.7") },

    { nxt_string("parseFloat('-5.7e+')"),
      nxt_string("-5.7") },

    { nxt_string("parseFloat('-5.7e+abc')"),
      nxt_string("-5.7") },

    /* JSON.parse() */

    { nxt_string("JSON.parse('null')"),
      nxt_string("null") },

    { nxt_string("JSON.parse('true')"),
      nxt_string("true") },

    { nxt_string("JSON.parse('false')"),
      nxt_string("false") },

    { nxt_string("JSON.parse('0')"),
      nxt_string("0") },

    { nxt_string("JSON.parse('-1234.56e2')"),
      nxt_string("-123456") },

    { nxt_string("typeof(JSON.parse('true'))"),
      nxt_string("boolean") },

    { nxt_string("typeof(JSON.parse('false'))"),
      nxt_string("boolean") },

    { nxt_string("typeof(JSON.parse('1'))"),
      nxt_string("number") },

    { nxt_string("typeof(JSON.parse('\"\"'))"),
      nxt_string("string") },

    { nxt_string("typeof(JSON.parse('{}'))"),
      nxt_string("object") },

    { nxt_string("typeof(JSON.parse('[]'))"),
      nxt_string("object") },

    { nxt_string("JSON.parse('\"abc\"')"),
      nxt_string("abc") },

    { nxt_string("JSON.parse('\"\\\\\"\"')"),
      nxt_string("\"") },

    { nxt_string("JSON.parse('\"\\\\n\"')"),
      nxt_string("\n") },

    { nxt_string("JSON.parse('\"\\\\t\"')"),
      nxt_string("\t") },

    { nxt_string("JSON.parse('\"ab\\\\\"c\"')"),
      nxt_string("ab\"c") },

    { nxt_string("JSON.parse('\"abcdefghijklmopqr\\\\\"s\"')"),
      nxt_string("abcdefghijklmopqr\"s") },

    { nxt_string("JSON.parse('\"ab\\\\\"c\"').length"),
      nxt_string("4") },

    { nxt_string("JSON.parse('\"аб\\\\\"в\"')"),
      nxt_string("аб\"в") },

    { nxt_string("JSON.parse('\"аб\\\\\"в\"').length"),
      nxt_string("4") },

    { nxt_string("JSON.parse('\"абвгдеёжзийкл\"').length"),
      nxt_string("13") },

    { nxt_string("JSON.parse('\"абвгдеёжзийкл\"').length"),
      nxt_string("13") },

    { nxt_string("JSON.parse('\"\\\\u03B1\"')"),
      nxt_string("α") },

    { nxt_string("JSON.parse('\"\\\\uD801\\\\uDC00\"')"),
      nxt_string("𐐀") },

    { nxt_string("JSON.parse('\"\\\\u03B1\"') == JSON.parse('\"\\\\u03b1\"')"),
      nxt_string("true") },

    { nxt_string("JSON.parse('\"\\\\u03B1\"').length"),
      nxt_string("1") },

    { nxt_string("JSON.parse('{\"a\":1}').a"),
      nxt_string("1") },

    { nxt_string("JSON.parse('{\"a\":1,\"a\":2}').a"),
      nxt_string("2") },

    { nxt_string("JSON.parse('{   \"a\" :  \"b\"   }').a"),
      nxt_string("b") },

    { nxt_string("JSON.parse('{\"a\":{\"b\":1}}').a.b"),
      nxt_string("1") },

    { nxt_string("JSON.parse('[{}, true ,1.1e2, {\"a\":[3,\"b\"]}]')[3].a[1]"),
      nxt_string("b") },

    { nxt_string("var o = JSON.parse('{\"a\":2}');"
                 "Object.getOwnPropertyDescriptor(o, 'a').configurable"),
      nxt_string("true") },

    { nxt_string("var o = JSON.parse('{\"a\":2}');"
                 "Object.getOwnPropertyDescriptor(o, 'a').writable"),
      nxt_string("true") },

    { nxt_string("var o = JSON.parse('{\"a\":2}');"
                 "Object.getOwnPropertyDescriptor(o, 'a').enumerable"),
      nxt_string("true") },

    { nxt_string("var o = JSON.parse('{\"a\":2}');"
                 "o.a = 3; o.a"),
      nxt_string("3") },

    { nxt_string("var o = JSON.parse('{\"a\":2}');"
                 "o.b = 3; o.b"),
      nxt_string("3") },

    { nxt_string("var o = JSON.parse('{}', function(k, v) {return v;}); o"),
      nxt_string("[object Object]") },

    { nxt_string("var o = JSON.parse('{\"a\":2, \"b\":4, \"a\":{}}',"
                 "                    function(k, v) {return undefined;});"
                 "o"),
      nxt_string("undefined") },

    { nxt_string("var o = JSON.parse('{\"a\":2, \"c\":4, \"b\":\"x\"}',"
                 "  function(k, v) {if (k === '' || typeof v === 'number') return v });"
                 "Object.keys(o)"),
      nxt_string("a,c") },

    { nxt_string("var o = JSON.parse('{\"a\":2, \"b\":{}}',"
                 "                    function(k, v) {return k;});"
                 "o+typeof(o)"),
      nxt_string("string") },

    { nxt_string("var o = JSON.parse('[\"a\", \"b\"]',"
                 "                    function(k, v) {return v;});"
                 "o"),
      nxt_string("a,b") },

    { nxt_string("var o = JSON.parse('{\"a\":[1,{\"b\":1},3]}',"
                 "                    function(k, v) {return v;});"
                 "o.a[1].b"),
      nxt_string("1") },

    { nxt_string("var o = JSON.parse('{\"a\":[1,2]}',"
                 "  function(k, v) {if (k === '' || k === 'a') {return v;}});"
                 "o.a"),
      nxt_string(",") },

    { nxt_string("var o = JSON.parse('{\"a\":[1,2]}',"
                 "  function(k, v) {return (k === '' || k === 'a') ? v : v*2});"
                 "o.a"),
      nxt_string("2,4") },

    { nxt_string("var o = JSON.parse('{\"a\":2, \"b\":{\"c\":[\"xx\"]}}',"
                 "   function(k, v) {return typeof v === 'number' ? v * 2 : v;});"
                 "o.a+o.b.c[0]"),
      nxt_string("4xx") },

    { nxt_string("var o = JSON.parse('{\"aa\":{\"b\":1}, \"abb\":1, \"c\":1}',"
                 "   function(k, v) {return (k === '' || /^a/.test(k)) ? v : undefined;});"
                 "Object.keys(o)"),
      nxt_string("aa,abb") },

    { nxt_string("var o = JSON.parse('{\"a\":\"x\"}',"
                 "   function(k, v) {if (k === 'a') {this.b='y';} return v});"
                 "o.a+o.b"),
      nxt_string("xy") },

    { nxt_string("var o = JSON.parse('{\"a\":\"x\"}',"
                 "   function(k, v) {return (k === 'a' ? {x:1} : v)});"
                 "o.a.x"),
      nxt_string("1") },

    { nxt_string("var keys = []; var o = JSON.parse('{\"a\":2, \"b\":{\"c\":\"xx\"}}',"
                 "   function(k, v) {keys.push(k); return v;});"
                 "keys"),
      nxt_string("a,c,b,") },

    { nxt_string("var args = []; var o = JSON.parse('[2,{\"a\":3}]',"
                 "   function(k, v) {args.push(k+\":\"+v); return v;});"
                 "args.join('|')"),
      nxt_string("0:2|a:3|1:[object Object]|:2,[object Object]") },

    { nxt_string("JSON.parse()"),
      nxt_string("SyntaxError: Unexpected token at position 0") },

    { nxt_string("JSON.parse([])"),
      nxt_string("SyntaxError: Unexpected end of input at position 0") },

    { nxt_string("JSON.parse('')"),
      nxt_string("SyntaxError: Unexpected end of input at position 0") },

    { nxt_string("JSON.parse('fals')"),
      nxt_string("SyntaxError: Unexpected token at position 0") },

    { nxt_string("JSON.parse(' t')"),
      nxt_string("SyntaxError: Unexpected token at position 1") },

    { nxt_string("JSON.parse('nu')"),
      nxt_string("SyntaxError: Unexpected token at position 0") },

    { nxt_string("JSON.parse('-')"),
      nxt_string("SyntaxError: Unexpected number at position 0") },

    { nxt_string("JSON.parse('--')"),
      nxt_string("SyntaxError: Unexpected number at position 1") },

    { nxt_string("JSON.parse('1-')"),
      nxt_string("SyntaxError: Unexpected token at position 1") },

    { nxt_string("JSON.parse('1ee1')"),
      nxt_string("SyntaxError: Unexpected token at position 1") },

    { nxt_string("JSON.parse('1eg')"),
      nxt_string("SyntaxError: Unexpected token at position 1") },

    { nxt_string("JSON.parse('0x01')"),
      nxt_string("SyntaxError: Unexpected token at position 1") },

    { nxt_string("JSON.parse('\"абв')"),
      nxt_string("SyntaxError: Unexpected end of input at position 4") },

    { nxt_string("JSON.parse('\"\b')"),
      nxt_string("SyntaxError: Forbidden source char at position 1") },

    { nxt_string("JSON.parse('\"\\\\u')"),
      nxt_string("SyntaxError: Unexpected end of input at position 3") },

    { nxt_string("JSON.parse('\"\\\\q\"')"),
      nxt_string("SyntaxError: Unknown escape char at position 2") },

    { nxt_string("JSON.parse('\"\\\\uDC01\"')"),
      nxt_string("SyntaxError: Invalid Unicode char at position 7") },

    { nxt_string("JSON.parse('\"\\\\uD801\\\\uE000\"')"),
      nxt_string("SyntaxError: Invalid surrogate pair at position 13") },

    { nxt_string("JSON.parse('{')"),
      nxt_string("SyntaxError: Unexpected end of input at position 1") },

    { nxt_string("JSON.parse('{{')"),
      nxt_string("SyntaxError: Unexpected token at position 1") },

    { nxt_string("JSON.parse('{[')"),
      nxt_string("SyntaxError: Unexpected token at position 1") },

    { nxt_string("JSON.parse('{\"a\"')"),
      nxt_string("SyntaxError: Unexpected token at position 4") },

    { nxt_string("JSON.parse('{\"a\":')"),
      nxt_string("SyntaxError: Unexpected end of input at position 5") },

    { nxt_string("JSON.parse('{\"a\":{')"),
      nxt_string("SyntaxError: Unexpected end of input at position 6") },

    { nxt_string("JSON.parse('{\"a\":{}')"),
      nxt_string("SyntaxError: Unexpected end of input at position 7") },

    { nxt_string("JSON.parse('{\"a\":{}g')"),
      nxt_string("SyntaxError: Unexpected token at position 7") },

    { nxt_string("JSON.parse('{\"a\":{},')"),
      nxt_string("SyntaxError: Unexpected end of input at position 8") },

    { nxt_string("JSON.parse('{\"a\":{},}')"),
      nxt_string("SyntaxError: Trailing comma at position 7") },

    { nxt_string("JSON.parse('{\"a\":{},,')"),
      nxt_string("SyntaxError: Unexpected token at position 8") },

    { nxt_string("JSON.parse('{\"a\":{},,}')"),
      nxt_string("SyntaxError: Unexpected token at position 8") },

    { nxt_string("JSON.parse('[')"),
      nxt_string("SyntaxError: Unexpected end of input at position 1") },

    { nxt_string("JSON.parse('[q')"),
      nxt_string("SyntaxError: Unexpected token at position 1") },

    { nxt_string("JSON.parse('[\"a')"),
      nxt_string("SyntaxError: Unexpected end of input at position 3") },

    { nxt_string("JSON.parse('[1 ')"),
      nxt_string("SyntaxError: Unexpected end of input at position 3") },

    { nxt_string("JSON.parse('[1,]')"),
      nxt_string("SyntaxError: Trailing comma at position 2") },

    { nxt_string("JSON.parse('[1 , 5 ')"),
      nxt_string("SyntaxError: Unexpected end of input at position 7") },

    { nxt_string("JSON.parse('{\"a\":'.repeat(32))"),
      nxt_string("SyntaxError: Nested too deep at position 155") },

    { nxt_string("JSON.parse('['.repeat(32))"),
      nxt_string("SyntaxError: Nested too deep at position 31") },

    { nxt_string("var o = JSON.parse('{', function(k, v) {return v;});o"),
      nxt_string("SyntaxError: Unexpected end of input at position 1") },

    { nxt_string("var o = JSON.parse('{\"a\":1}', "
                 "                   function(k, v) {return v.a.a;}); o"),
      nxt_string("TypeError: cannot get property 'a' of undefined") },

    /* JSON.stringify() */

    { nxt_string("JSON.stringify()"),
      nxt_string("undefined") },

    { nxt_string("JSON.stringify('')"),
      nxt_string("\"\"") },

    { nxt_string("JSON.stringify('abc')"),
      nxt_string("\"abc\"") },

    { nxt_string("JSON.stringify(new String('abc'))"),
      nxt_string("\"abc\"") },

    { nxt_string("JSON.stringify(123)"),
      nxt_string("123") },

    { nxt_string("JSON.stringify(-0)"),
      nxt_string("0") },

    { nxt_string("JSON.stringify(0.00000123)"),
      nxt_string("0.00000123") },

    { nxt_string("JSON.stringify(new Number(123))"),
      nxt_string("123") },

    { nxt_string("JSON.stringify(true)"),
      nxt_string("true") },

    { nxt_string("JSON.stringify(false)"),
      nxt_string("false") },

    { nxt_string("JSON.stringify(new Boolean(1))"),
      nxt_string("true") },

    { nxt_string("JSON.stringify(new Boolean(0))"),
      nxt_string("false") },

    { nxt_string("JSON.stringify(null)"),
      nxt_string("null") },

    { nxt_string("JSON.stringify(undefined)"),
      nxt_string("undefined") },

    { nxt_string("JSON.stringify({})"),
      nxt_string("{}") },

    { nxt_string("JSON.stringify([])"),
      nxt_string("[]") },

    { nxt_string("var a = [1]; a[2] = 'x'; JSON.stringify(a)"),
      nxt_string("[1,null,\"x\"]") },

    { nxt_string("JSON.stringify({a:\"b\",c:19,e:null,t:true,f:false})"),
      nxt_string("{\"a\":\"b\",\"c\":19,\"e\":null,\"t\":true,\"f\":false}") },

    { nxt_string("JSON.stringify({a:1, b:undefined})"),
      nxt_string("{\"a\":1}") },

    { nxt_string("var o = {a:1, c:2};"
                 "Object.defineProperty(o, 'b', {enumerable:false, value:3});"
                 "JSON.stringify(o)"),
      nxt_string("{\"a\":1,\"c\":2}") },

    { nxt_string("JSON.stringify({a:{}, b:[function(v){}]})"),
      nxt_string("{\"a\":{},\"b\":[null]}") },

    { nxt_string("JSON.stringify(RegExp())"),
      nxt_string("{}") },

    { nxt_string("JSON.stringify(SyntaxError('e'))"),
      nxt_string("{}") },

    { nxt_string("JSON.stringify(URIError('e'))"),
      nxt_string("{}") },

    { nxt_string("var e = URIError('e'); e.name = 'E'; JSON.stringify(e)"),
      nxt_string("{\"name\":\"E\"}") },

    { nxt_string("var e = URIError('e'); e.message = 'E'; JSON.stringify(e)"),
      nxt_string("{}") },

    { nxt_string("var e = URIError('e'); e.foo = 'E'; JSON.stringify(e)"),
      nxt_string("{\"foo\":\"E\"}") },

    { nxt_string("JSON.stringify([$r])"),
      nxt_string("[null]") },

    /* Ignoring named properties of an array. */

    { nxt_string("var a = [1,2]; a.a = 1;"
                 "JSON.stringify(a)"),
      nxt_string("[1,2]") },

    { nxt_string("JSON.stringify({a:{b:{c:{d:1}, e:function(v){}}}})"),
      nxt_string("{\"a\":{\"b\":{\"c\":{\"d\":1}}}}") },

    { nxt_string("JSON.stringify([[\"b\",undefined],1,[5],{a:1}])"),
      nxt_string("[[\"b\",null],1,[5],{\"a\":1}]") },

    { nxt_string("var json = '{\"a\":{\"b\":{\"c\":{\"d\":1},\"e\":[true]}}}';"
                 "json == JSON.stringify(JSON.parse(json))"),
      nxt_string("true") },

    { nxt_string("var json = '{\"a\":\"абв\",\"b\":\"α\"}';"
                 "json == JSON.stringify(JSON.parse(json))"),
      nxt_string("true") },

    /* Multibyte characters: z - 1 byte, α - 2 bytes, 𐐀 - 4 bytes */

    { nxt_string("JSON.stringify('α𐐀z'.repeat(10))"),
      nxt_string("\"α𐐀zα𐐀zα𐐀zα𐐀zα𐐀zα𐐀zα𐐀zα𐐀zα𐐀zα𐐀z\"") },

    { nxt_string("JSON.stringify('α𐐀z'.repeat(10)).length"),
      nxt_string("32") },

    { nxt_string("JSON.stringify('a\nbc')"),
      nxt_string("\"a\\nbc\"") },

    { nxt_string("JSON.stringify('а\tбв')"),
      nxt_string("\"а\\tбв\"") },

    { nxt_string("JSON.stringify('\n\t\r\"\f\b ')"),
      nxt_string("\"\\n\\t\\r\\\"\\f\\b \"") },

    { nxt_string("JSON.stringify('\x00\x01\x02\x1f')"),
      nxt_string("\"\\u0000\\u0001\\u0002\\u001F\"") },

    { nxt_string("JSON.stringify('abc\x00')"),
      nxt_string("\"abc\\u0000\"") },

    { nxt_string("JSON.stringify('\x00zz')"),
      nxt_string("\"\\u0000zz\"") },

    { nxt_string("JSON.stringify('\x00')"),
      nxt_string("\"\\u0000\"") },

    { nxt_string("JSON.stringify('a\x00z')"),
      nxt_string("\"a\\u0000z\"") },

    { nxt_string("JSON.stringify('\x00z\x00')"),
      nxt_string("\"\\u0000z\\u0000\"") },

    { nxt_string("var i, s, r = true;"
                 " for (i = 0; i < 128; i++) {"
                 "  s = 'α𐐀z'.repeat(i);"
                 "  r &= (JSON.stringify(s) == ('\"' + s + '\"'));"
                 "}; r"),
      nxt_string("1") },

    { nxt_string("JSON.stringify('\\u0000'.repeat(10)) == ('\"' + '\\\\u0000'.repeat(10) + '\"')"),
      nxt_string("true") },

    { nxt_string("JSON.stringify('abc'.repeat(100)).length"),
      nxt_string("302") },

    { nxt_string("JSON.stringify('абв'.repeat(100)).length"),
      nxt_string("302") },

    /* Byte strings. */

    { nxt_string("JSON.stringify('\\u00CE\\u00B1\\u00C2\\u00B6'.toBytes())"),
      nxt_string("\"α¶\"") },

    { nxt_string("JSON.stringify('µ§±®'.toBytes())"),
      nxt_string("\"\xB5\xA7\xB1\xAE\"") },

    /* Optional arguments. */

    { nxt_string("JSON.stringify(undefined, undefined, 1)"),
      nxt_string("undefined") },

    { nxt_string("JSON.stringify([{a:1,b:{c:2}},1], undefined, 0)"),
      nxt_string("[{\"a\":1,\"b\":{\"c\":2}},1]") },

    { nxt_string("JSON.stringify([{a:1,b:{c:2}},1], undefined, 1)"),
      nxt_string("[\n {\n  \"a\": 1,\n  \"b\": {\n   \"c\": 2\n  }\n },\n 1\n]") },

    { nxt_string("JSON.stringify([{a:1,b:{c:2}},1], undefined, ' ')"),
      nxt_string("[\n {\n  \"a\": 1,\n  \"b\": {\n   \"c\": 2\n  }\n },\n 1\n]") },

    { nxt_string("JSON.stringify([{a:1,b:{c:2}},1], undefined, '#')"),
      nxt_string("[\n#{\n##\"a\": 1,\n##\"b\": {\n###\"c\": 2\n##}\n#},\n#1\n]") },

    { nxt_string("JSON.stringify([1], undefined, 'AAAAABBBBBC')"),
      nxt_string("[\nAAAAABBBBB1\n]") },

    { nxt_string("JSON.stringify([1], undefined, 11)"),
      nxt_string("[\n          1\n]") },

    { nxt_string("JSON.stringify([{a:1,b:{c:2}},1], undefined, -1)"),
      nxt_string("[{\"a\":1,\"b\":{\"c\":2}},1]") },

    { nxt_string("JSON.stringify([{a:1,b:{c:2}},1], undefined, new Date())"),
      nxt_string("[{\"a\":1,\"b\":{\"c\":2}},1]") },

    { nxt_string("JSON.stringify({toJSON:function(k){}})"),
      nxt_string("undefined") },

    { nxt_string("JSON.stringify({toJSON:function(k){return k}})"),
      nxt_string("\"\"") },

    { nxt_string("JSON.stringify(new Date(1308895323625))"),
      nxt_string("\"2011-06-24T06:02:03.625Z\"") },

    { nxt_string("JSON.stringify({a:new Date(1308895323625)})"),
      nxt_string("{\"a\":\"2011-06-24T06:02:03.625Z\"}") },

    { nxt_string("JSON.stringify({b:{toJSON:function(k){return undefined}}})"),
      nxt_string("{}") },

    { nxt_string("JSON.stringify({b:{toJSON:function(k){}},c:1})"),
      nxt_string("{\"c\":1}") },

    { nxt_string("JSON.stringify({b:{toJSON:function(k){return k}}})"),
      nxt_string("{\"b\":\"b\"}") },

    { nxt_string("JSON.stringify({a:1,b:new Date(1308895323625),c:2})"),
      nxt_string("{\"a\":1,\"b\":\"2011-06-24T06:02:03.625Z\",\"c\":2}") },

    { nxt_string("JSON.stringify({a:{b:new Date(1308895323625)}})"),
      nxt_string("{\"a\":{\"b\":\"2011-06-24T06:02:03.625Z\"}}") },

    { nxt_string("function key(k){return k}; function und(k){}"
                 "JSON.stringify([{toJSON:key},{toJSON:und},{toJSON:key}])"),
      nxt_string("[\"0\",null,\"2\"]") },

    { nxt_string("JSON.stringify({b:{a:1,c:[2]}}, function(k,v){return v})"),
      nxt_string("{\"b\":{\"a\":1,\"c\":[2]}}") },

    { nxt_string("JSON.stringify([{a:1}, 2], function(k,v){return v})"),
      nxt_string("[{\"a\":1},2]") },

    { nxt_string("JSON.stringify({a:{toJSON:function(k){}}}, function(k,v){return v})"),
      nxt_string("{}") },

    { nxt_string("JSON.stringify({a:{toJSON:function(k){return 1}}}, function(k,v){return v})"),
      nxt_string("{\"a\":1}") },

    { nxt_string("JSON.stringify([{toJSON:function(k){}}], function(k,v){return v})"),
      nxt_string("[null]") },

    { nxt_string("JSON.stringify([{toJSON:function(k){return 1}}], function(k,v){return v})"),
      nxt_string("[1]") },

    { nxt_string("JSON.stringify({a:new Date(1308895323625)}, function(k,v){return v})"),
      nxt_string("{\"a\":\"2011-06-24T06:02:03.625Z\"}") },

    { nxt_string("JSON.stringify([new Date(1308895323625)], function(k,v){return v})"),
      nxt_string("[\"2011-06-24T06:02:03.625Z\"]") },

    { nxt_string("JSON.stringify([new Date(1308895323625)], "
                 "  function(k,v){return (typeof v === 'string') ? v.toLowerCase() : v})"),
      nxt_string("[\"2011-06-24t06:02:03.625z\"]") },

    { nxt_string("JSON.stringify([new Date(1308895323625)], "
                 "  function(k,v){return (typeof v === 'string') ? v.toLowerCase() : v}, '#')"),
      nxt_string("[\n#\"2011-06-24t06:02:03.625z\"\n]") },

    { nxt_string("JSON.stringify({a:new Date(1308895323625),b:1,c:'a'}, "
                 "  function(k,v){return (typeof v === 'string') ? undefined : v})"),
      nxt_string("{\"b\":1}") },

    { nxt_string("JSON.stringify({a:new Date(1308895323625),b:1,c:'a'}, "
                 "  function(k,v){return (typeof v === 'string') ? undefined : v}, '#')"),
      nxt_string("{\n#\"b\": 1\n}") },

    { nxt_string("JSON.stringify([new Date(1308895323625),1,'a'], "
                 "  function(k,v){return (typeof v === 'string') ? undefined : v})"),
      nxt_string("[null,1,null]") },

    { nxt_string("var keys = []; var o = JSON.stringify({a:2, b:{c:1}},"
                 "   function(k, v) {keys.push(k); return v;});"
                 "keys"),
      nxt_string(",a,b,c") },

    { nxt_string("JSON.stringify(['a', 'b', 'c'], "
                 "    function(i, v) { if (i === '0') {return undefined} "
                 "                     else if (i == 1) {return 2} "
                 "                     else {return v}})"),
      nxt_string("[null,2,\"c\"]") },

    { nxt_string("JSON.stringify({a:2, b:{c:1}},"
                 "               function(k, v) {delete this['b']; return v;})"),
      nxt_string("{\"a\":2}") },

    { nxt_string("JSON.stringify(JSON.parse('{\"a\":1,\"b\":2}', "
                 "          function(k, v) {delete this['b']; return v;}))"),
      nxt_string("{\"a\":1}") },

    { nxt_string("var keys = []; var o = JSON.stringify([[1,2],{a:3}, 4],"
                 "   function(k, v) {keys.push(k); return v;});"
                 "keys"),
      nxt_string(",0,0,1,1,a,2") },

    { nxt_string("JSON.stringify({b:{a:1,c:[2]}}, ['a', undefined, 'b', {}, 'a'])"),
      nxt_string("{\"b\":{\"a\":1}}") },

    { nxt_string("JSON.stringify({b:{a:1,c:[2]}}, [new String('a'), new String('b')])"),
      nxt_string("{\"b\":{\"a\":1}}") },

    { nxt_string("JSON.stringify({'1':1,'2':2,'3':3}, [1, new Number(2)])"),
      nxt_string("{\"1\":1,\"2\":2}") },

    { nxt_string("var objs = []; var o = JSON.stringify({a:1},"
                 "   function(k, v) {objs.push(this); return v});"
                 "JSON.stringify(objs)"),
      nxt_string("[{\"\":{\"a\":1}},{\"a\":1}]") },

    { nxt_string("var a = []; a[0] = a; JSON.stringify(a)"),
      nxt_string("TypeError: Nested too deep or a cyclic structure") },

    { nxt_string("var a = {}; a.a = a; JSON.stringify(a)"),
      nxt_string("TypeError: Nested too deep or a cyclic structure") },

    /* njs.dump(). */

    { nxt_string("njs.dump({a:1, b:[1,,2,{c:new Boolean(1)}]})"),
      nxt_string("{a:1,b:[1,<empty>,2,{c:[Boolean: true]}]}") },

    { nxt_string("njs.dump(Array.prototype.slice.call({'1':'b', length:2}))"),
      nxt_string("[<empty>,'b']") },

    { nxt_string("njs.dump($r.props)"),
      nxt_string("{a:{type:\"property\",props:[\"getter\"]},b:{type:\"property\",props:[\"getter\"]}}") },

    { nxt_string("njs.dump($r.header)"),
      nxt_string("{type:\"object\",props:[\"getter\",\"foreach\",\"next\"]}") },

    /* require(). */

    { nxt_string("require('unknown_module')"),
      nxt_string("Error: Cannot find module 'unknown_module'") },

    { nxt_string("require()"),
      nxt_string("TypeError: missing path") },

    { nxt_string("var fs = require('fs'); typeof fs"),
      nxt_string("object") },

    /* require('fs').readFile() */

    { nxt_string("var fs = require('fs');"
                 "fs.readFile()"),
      nxt_string("TypeError: too few arguments") },

    { nxt_string("var fs = require('fs');"
                 "fs.readFile('/njs_unknown_path')"),
      nxt_string("TypeError: too few arguments") },

    { nxt_string("var fs = require('fs');"
                 "fs.readFile('/njs_unknown_path', {flag:'xx'})"),
      nxt_string("TypeError: callback must be a function") },

    { nxt_string("var fs = require('fs');"
                 "fs.readFile('/njs_unknown_path', {flag:'xx'}, 1)"),
      nxt_string("TypeError: callback must be a function") },

    { nxt_string("var fs = require('fs');"
                 "fs.readFile('/njs_unknown_path', {flag:'xx'}, function () {})"),
      nxt_string("TypeError: Unknown file open flags: 'xx'") },

    { nxt_string("var fs = require('fs');"
                 "fs.readFile('/njs_unknown_path', {encoding:'ascii'}, function () {})"),
      nxt_string("TypeError: Unknown encoding: 'ascii'") },

    { nxt_string("var fs = require('fs');"
                 "fs.readFile('/njs_unknown_path', 'ascii', function () {})"),
      nxt_string("TypeError: Unknown encoding: 'ascii'") },

    /* require('fs').readFileSync() */

    { nxt_string("var fs = require('fs');"
                 "fs.readFileSync()"),
      nxt_string("TypeError: too few arguments") },

    { nxt_string("var fs = require('fs');"
                 "fs.readFileSync({})"),
      nxt_string("TypeError: path must be a string") },

    { nxt_string("var fs = require('fs');"
                 "fs.readFileSync('/njs_unknown_path', {flag:'xx'})"),
      nxt_string("TypeError: Unknown file open flags: 'xx'") },

    { nxt_string("var fs = require('fs');"
                 "fs.readFileSync('/njs_unknown_path', {encoding:'ascii'})"),
      nxt_string("TypeError: Unknown encoding: 'ascii'") },

    { nxt_string("var fs = require('fs');"
                 "fs.readFileSync('/njs_unknown_path', 'ascii')"),
      nxt_string("TypeError: Unknown encoding: 'ascii'") },

    { nxt_string("var fs = require('fs');"
                 "fs.readFileSync('/njs_unknown_path', true)"),
      nxt_string("TypeError: Unknown options type (a string or object required)") },


    /* require('fs').writeFile() */

    { nxt_string("var fs = require('fs');"
                 "fs.writeFile()"),
      nxt_string("TypeError: too few arguments") },

    { nxt_string("var fs = require('fs');"
                 "fs.writeFile('/njs_unknown_path')"),
      nxt_string("TypeError: too few arguments") },

    { nxt_string("var fs = require('fs');"
                 "fs.writeFile('/njs_unknown_path', '')"),
      nxt_string("TypeError: too few arguments") },

    { nxt_string("var fs = require('fs');"
                 "fs.writeFile({}, '', function () {})"),
      nxt_string("TypeError: path must be a string") },

    { nxt_string("var fs = require('fs');"
                 "fs.writeFile('/njs_unknown_path', '', 'utf8')"),
      nxt_string("TypeError: callback must be a function") },

    { nxt_string("var fs = require('fs');"
                 "fs.writeFile('/njs_unknown_path', '', {flag:'xx'}, function () {})"),
      nxt_string("TypeError: Unknown file open flags: 'xx'") },

    { nxt_string("var fs = require('fs');"
                 "fs.writeFile('/njs_unknown_path', '', {encoding:'ascii'}, function () {})"),
      nxt_string("TypeError: Unknown encoding: 'ascii'") },

    { nxt_string("var fs = require('fs');"
                 "fs.writeFile('/njs_unknown_path', '', 'ascii', function () {})"),
      nxt_string("TypeError: Unknown encoding: 'ascii'") },

    { nxt_string("var fs = require('fs');"
                 "fs.writeFile('/njs_unknown_path', '', true, function () {})"),
      nxt_string("TypeError: Unknown options type (a string or object required)") },

    /* require('fs').writeFileSync() */

    { nxt_string("var fs = require('fs');"
                 "fs.writeFileSync()"),
      nxt_string("TypeError: too few arguments") },

    { nxt_string("var fs = require('fs');"
                 "fs.writeFileSync('/njs_unknown_path')"),
      nxt_string("TypeError: too few arguments") },

    { nxt_string("var fs = require('fs');"
                 "fs.writeFileSync({}, '')"),
      nxt_string("TypeError: path must be a string") },

    { nxt_string("var fs = require('fs');"
                 "fs.writeFileSync('/njs_unknown_path', '', {flag:'xx'})"),
      nxt_string("TypeError: Unknown file open flags: 'xx'") },

    { nxt_string("var fs = require('fs');"
                 "fs.writeFileSync('/njs_unknown_path', '', {encoding:'ascii'})"),
      nxt_string("TypeError: Unknown encoding: 'ascii'") },

    { nxt_string("var fs = require('fs');"
                 "fs.writeFileSync('/njs_unknown_path', '', 'ascii')"),
      nxt_string("TypeError: Unknown encoding: 'ascii'") },

    { nxt_string("var fs = require('fs');"
                 "fs.writeFileSync('/njs_unknown_path', '', true)"),
      nxt_string("TypeError: Unknown options type (a string or object required)") },

    /* require('crypto').createHash() */

    { nxt_string("require('crypto').createHash('sha1')"),
      nxt_string("[object Hash]") },

    { nxt_string("Object.prototype.toString.call(require('crypto').createHash('sha1'))"),
      nxt_string("[object Object]") },

    { nxt_string("var h = require('crypto').createHash('md5');"
                 "h.update('AB').digest('hex')"),
      nxt_string("b86fc6b051f63d73de262d4c34e3a0a9") },

    { nxt_string("var h = require('crypto').createHash('sha1');"
                 "h.update('A').update('B').digest('hex')"),
      nxt_string("06d945942aa26a61be18c3e22bf19bbca8dd2b5d") },

    { nxt_string("var h = require('crypto').createHash('sha1');"
                 "h.update('AB').digest('hex')"),
      nxt_string("06d945942aa26a61be18c3e22bf19bbca8dd2b5d") },

    { nxt_string("var h = require('crypto').createHash('sha1');"
                 "h.update('AB').digest().toString('hex')"),
      nxt_string("06d945942aa26a61be18c3e22bf19bbca8dd2b5d") },

    { nxt_string("var h = require('crypto').createHash('sha1');"
                 "h.update('AB').digest('base64')"),
      nxt_string("BtlFlCqiamG+GMPiK/GbvKjdK10=") },

    { nxt_string("var h = require('crypto').createHash('sha1');"
                 "h.update('AB').digest('base64url')"),
      nxt_string("BtlFlCqiamG-GMPiK_GbvKjdK10") },

    { nxt_string("var h = require('crypto').createHash('sha1');"
                 "h.update('AB').digest().toString('base64')"),
      nxt_string("BtlFlCqiamG+GMPiK/GbvKjdK10=") },

    { nxt_string("var h = require('crypto').createHash('sha1');"
                 "h.update('abc'.repeat(100)).digest('hex')"),
      nxt_string("c95466320eaae6d19ee314ae4f135b12d45ced9a") },

    { nxt_string("var h = require('crypto').createHash('sha256');"
                 "h.update('A').update('B').digest('hex')"),
      nxt_string("38164fbd17603d73f696b8b4d72664d735bb6a7c88577687fd2ae33fd6964153") },

    { nxt_string("var h = require('crypto').createHash('sha256');"
                 "h.update('AB').digest('hex')"),
      nxt_string("38164fbd17603d73f696b8b4d72664d735bb6a7c88577687fd2ae33fd6964153") },

    { nxt_string("var h = require('crypto').createHash('sha256');"
                 "h.update('abc'.repeat(100)).digest('hex')"),
      nxt_string("d9f5aeb06abebb3be3f38adec9a2e3b94228d52193be923eb4e24c9b56ee0930") },

    { nxt_string("var h = require('crypto').createHash()"),
      nxt_string("TypeError: algorithm must be a string") },

    { nxt_string("var h = require('crypto').createHash([])"),
      nxt_string("TypeError: algorithm must be a string") },

    { nxt_string("var h = require('crypto').createHash('sha512')"),
      nxt_string("TypeError: not supported algorithm: 'sha512'") },

    { nxt_string("var h = require('crypto').createHash('sha1');"
                 "h.update()"),
      nxt_string("TypeError: data must be a string") },

    { nxt_string("var h = require('crypto').createHash('sha1');"
                 "h.update({})"),
      nxt_string("TypeError: data must be a string") },

    { nxt_string("var h = require('crypto').createHash('sha1');"
                 "h.update('A').digest('latin1')"),
      nxt_string("TypeError: Unknown digest encoding: 'latin1'") },

    { nxt_string("var h = require('crypto').createHash('sha1');"
                 "h.update('A').digest('hex'); h.digest('hex')"),
      nxt_string("Error: Digest already called") },

    { nxt_string("var h = require('crypto').createHash('sha1');"
                 "h.update('A').digest('hex'); h.update('B')"),
      nxt_string("Error: Digest already called") },

    { nxt_string("typeof require('crypto').createHash('md5')"),
      nxt_string("object") },

    /* require('crypto').createHmac() */

    { nxt_string("require('crypto').createHmac('sha1', '')"),
      nxt_string("[object Hmac]") },

    { nxt_string("var h = require('crypto').createHmac('md5', '');"
                 "h.digest('hex')"),
      nxt_string("74e6f7298a9c2d168935f58c001bad88") },

    { nxt_string("var h = require('crypto').createHmac('sha1', '');"
                 "h.digest('hex')"),
      nxt_string("fbdb1d1b18aa6c08324b7d64b71fb76370690e1d") },

    { nxt_string("var h = require('crypto').createHmac('sha1', '');"
                 "h.digest().toString('hex')"),
      nxt_string("fbdb1d1b18aa6c08324b7d64b71fb76370690e1d") },

    { nxt_string("var h = require('crypto').createHmac('md5', 'secret key');"
                 "h.update('AB').digest('hex')"),
      nxt_string("9c72728915eb26620a5caeafd0063b29") },

    { nxt_string("var h = require('crypto').createHmac('sha1', 'secret key');"
                 "h.update('A').update('B').digest('hex')"),
      nxt_string("adc60e03459c4bae7cf4eb6d9730003e9490b22f") },

    { nxt_string("var h = require('crypto').createHmac('sha1', 'secret key');"
                 "h.update('AB').digest('hex')"),
      nxt_string("adc60e03459c4bae7cf4eb6d9730003e9490b22f") },

    { nxt_string("var h = require('crypto').createHmac('sha1', 'secret key');"
                 "h.update('AB').digest('base64')"),
      nxt_string("rcYOA0WcS6589OttlzAAPpSQsi8=") },

    { nxt_string("var h = require('crypto').createHmac('sha1', 'secret key');"
                 "h.update('AB').digest('base64url')"),
      nxt_string("rcYOA0WcS6589OttlzAAPpSQsi8") },

    { nxt_string("var h = require('crypto').createHmac('sha1', 'secret key');"
                 "h.update('AB').digest().toString('base64')"),
      nxt_string("rcYOA0WcS6589OttlzAAPpSQsi8=") },

    { nxt_string("var h = require('crypto').createHmac('sha1', 'secret key');"
                 "h.update('abc'.repeat(100)).digest('hex')"),
      nxt_string("b105ad6921e4c54d3fa0a9ec3f7f0ee9bd2c659d") },

    { nxt_string("var h = require('crypto').createHmac('sha1', 'A'.repeat(40));"
                 "h.update('AB').digest('hex')"),
      nxt_string("0b84f78ca5275d76d4b7dafb5845ee2b6a79c4c2") },

    { nxt_string("var h = require('crypto').createHmac('sha1', 'A'.repeat(64));"
                 "h.update('AB').digest('hex')"),
      nxt_string("400ce530816c6b3247e2959f3982a12aaf58c0c9") },

    { nxt_string("var h = require('crypto').createHmac('sha1', 'A'.repeat(100));"
                 "h.update('AB').digest('hex')"),
      nxt_string("670e7cdebae6392797e000e79e51d3b6589d8fad") },

    { nxt_string("var h = require('crypto').createHmac('sha256', '');"
                 "h.digest('hex')"),
      nxt_string("b613679a0814d9ec772f95d778c35fc5ff1697c493715653c6c712144292c5ad") },

    { nxt_string("var h = require('crypto').createHmac('sha256', 'secret key');"
                 "h.update('A').update('B').digest('hex')"),
      nxt_string("46085184b3b45a13d838bf71a0ce03675dab30931e0f1f68fa636ea65fdb286d") },

    { nxt_string("var h = require('crypto').createHmac('sha256', 'secret key');"
                 "h.update('AB').digest('hex')"),
      nxt_string("46085184b3b45a13d838bf71a0ce03675dab30931e0f1f68fa636ea65fdb286d") },

    { nxt_string("var h = require('crypto').createHmac('sha256', 'A'.repeat(64));"
                 "h.update('AB').digest('hex')"),
      nxt_string("ee9dce43b12eb3e865614ad9c1a8d4fad4b6eac2b64647bd24cd192888d3f367") },

    { nxt_string("var h = require('crypto').createHmac('sha256', 'A'.repeat(100));"
                 "h.update('AB').digest('hex')"),
      nxt_string("5647b6c429701ff512f0f18232b4507065d2376ca8899a816a0a6e721bf8ddcc") },

    { nxt_string("var h = require('crypto').createHmac('md5', 'secret key');"
                 "h.update('abc'.repeat(100)).digest('hex')"),
      nxt_string("5dd706af43536f8c9c83e7ea55b1a5a2") },

    { nxt_string("var h = require('crypto').createHmac('sha1', 'secret key');"
                 "h.update('abc'.repeat(100)).digest('hex')"),
      nxt_string("b105ad6921e4c54d3fa0a9ec3f7f0ee9bd2c659d") },

    { nxt_string("var h = require('crypto').createHmac('sha256', 'secret key');"
                 "h.update('abc'.repeat(100)).digest('hex')"),
      nxt_string("f6550d398ce350ee8d94a0f44f2cf6b9bc8d316ae4625fb4434f22980a276bac") },

    { nxt_string("var h = require('crypto').createHmac()"),
      nxt_string("TypeError: algorithm must be a string") },

    { nxt_string("var h = require('crypto').createHmac([])"),
      nxt_string("TypeError: algorithm must be a string") },

    { nxt_string("var h = require('crypto').createHmac('sha512', '')"),
      nxt_string("TypeError: not supported algorithm: 'sha512'") },

    { nxt_string("var h = require('crypto').createHmac('sha1', [])"),
      nxt_string("TypeError: key must be a string") },

    { nxt_string("var h = require('crypto').createHmac('sha1', 'secret key');"
                 "h.update('A').digest('hex'); h.digest('hex')"),
      nxt_string("Error: Digest already called") },

    { nxt_string("var h = require('crypto').createHmac('sha1', 'secret key');"
                 "h.update('A').digest('hex'); h.update('B')"),
      nxt_string("Error: Digest already called") },

    { nxt_string("typeof require('crypto').createHmac('md5', 'a')"),
      nxt_string("object") },

    /* setTimeout(). */

    { nxt_string("setTimeout()"),
      nxt_string("TypeError: too few arguments") },

    { nxt_string("setTimeout(function(){})"),
      nxt_string("InternalError: not supported by host environment") },

    { nxt_string("setTimeout(function(){}, 12)"),
      nxt_string("InternalError: not supported by host environment") },

    /* clearTimeout(). */

    { nxt_string("clearTimeout()"),
      nxt_string("undefined") },

    { nxt_string("clearTimeout(123)"),
      nxt_string("undefined") },

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
      nxt_string("SyntaxError: Unexpected token \"null\" in 1") },

    /* es5id: 8.2_A3 */

    { nxt_string("typeof(null) === \"object\""),
      nxt_string("true") },

};


static njs_unit_test_t  njs_tz_test[] =
{
     { nxt_string("var d = new Date(1); d = d + ''; d.slice(0, 33)"),
       nxt_string("Thu Jan 01 1970 12:45:00 GMT+1245") },

     { nxt_string("var d = new Date(2011, 5, 24, 18, 45); d.getTime()"),
       nxt_string("1308895200000") },

     { nxt_string("var d = new Date(2011, 5, 24, 18, 45); d.valueOf()"),
       nxt_string("1308895200000") },

     { nxt_string("var d = new Date(2011, 5, 24, 18, 45);"
                  "d.toString().slice(0, 33)"),
       nxt_string("Fri Jun 24 2011 18:45:00 GMT+1245") },

     { nxt_string("var d = new Date(2011, 5, 24, 18, 45);"
                  "d.toTimeString().slice(0, 17)"),
       nxt_string("18:45:00 GMT+1245") },

     { nxt_string("var d = new Date(2011, 5, 24, 18, 45); d.toUTCString()"),
       nxt_string("Fri Jun 24 2011 06:00:00 GMT") },

     { nxt_string("var d = new Date(2011, 5, 24, 18, 45, 12, 625);"
                  "d.toISOString()"),
       nxt_string("2011-06-24T06:00:12.625Z") },

     { nxt_string("var d = new Date(2011, 5, 24, 18, 45); d.getUTCHours()"),
       nxt_string("6") },

     { nxt_string("var d = new Date(2011, 5, 24, 18, 45); d.getUTCMinutes()"),
       nxt_string("0") },

     { nxt_string("var d = new Date(2011, 5, 24, 18, 45, 12, 625);"
                  "d.getTimezoneOffset()"),
       nxt_string("-765") },

     { nxt_string("var d = new Date(1308895323625); d.setMinutes(3, 2, 5003);"
                  "d.getTime()"),
       nxt_string("1308892687003") },

     { nxt_string("var d = new Date(1308895323625); d.setMinutes(3, 2);"
                  "d.getTime()"),
       nxt_string("1308892682625") },

     { nxt_string("var d = new Date(1308895323625); d.setMinutes(3);"
                  "d.getTime()"),
       nxt_string("1308892683625") },

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

     { nxt_string("var d = new Date(1308895323625); d.setMonth(2, 10);"
                  "d.getTime()"),
       nxt_string("1299733323625") },

     { nxt_string("var d = new Date(1308895323625); d.setMonth(2);"
                  "d.getTime()"),
       nxt_string("1300942923625") },

     { nxt_string("var d = new Date(1308895323625); d.setFullYear(2010, 2, 10);"
                  "d.getTime()"),
       nxt_string("1268197323625") },

     { nxt_string("var d = new Date(1308895323625); d.setFullYear(2010, 2);"
                  "d.getTime()"),
       nxt_string("1269406923625") },

     { nxt_string("var d = new Date(2011, 5, 24, 18, 45, 12, 625);"
                  "d.toJSON(1)"),
       nxt_string("2011-06-24T06:00:12.625Z") },
};


typedef struct {
    nxt_lvlhsh_t          hash;
    const njs_extern_t    *proto;
    nxt_mem_cache_pool_t  *mem_cache_pool;

    uint32_t              a;
    nxt_str_t             uri;

    njs_opaque_value_t    value;
} njs_unit_test_req_t;


typedef struct {
    njs_value_t           name;
    njs_value_t           value;
} njs_unit_test_prop_t;


static nxt_int_t
lvlhsh_unit_test_key_test(nxt_lvlhsh_query_t *lhq, void *data)
{
    nxt_str_t             name;
    njs_unit_test_prop_t  *prop;

    prop = data;
    njs_string_get(&prop->name, &name);

    if (name.length != lhq->key.length) {
        return NXT_DECLINED;
    }

    if (memcmp(name.start, lhq->key.start, lhq->key.length) == 0) {
        return NXT_OK;
    }

    return NXT_DECLINED;
}


static void *
lvlhsh_unit_test_pool_alloc(void *pool, size_t size, nxt_uint_t nalloc)
{
    return nxt_mem_cache_align(pool, size, size);
}


static void
lvlhsh_unit_test_pool_free(void *pool, void *p, size_t size)
{
    nxt_mem_cache_free(pool, p);
}


static const nxt_lvlhsh_proto_t  lvlhsh_proto  nxt_aligned(64) = {
    NXT_LVLHSH_LARGE_SLAB,
    0,
    lvlhsh_unit_test_key_test,
    lvlhsh_unit_test_pool_alloc,
    lvlhsh_unit_test_pool_free,
};


static njs_unit_test_prop_t *
lvlhsh_unit_test_alloc(nxt_mem_cache_pool_t *pool, const njs_value_t *name,
    const njs_value_t *value)
{
    njs_unit_test_prop_t *prop;

    prop = nxt_mem_cache_alloc(pool, sizeof(njs_unit_test_prop_t));
    if (prop == NULL) {
        return NULL;
    }

    prop->name = *name;
    prop->value = *value;

    return prop;
}


static nxt_int_t
lvlhsh_unit_test_add(njs_unit_test_req_t *r, njs_unit_test_prop_t *prop)
{
    nxt_lvlhsh_query_t  lhq;

    njs_string_get(&prop->name, &lhq.key);
    lhq.key_hash = nxt_djb_hash(lhq.key.start, lhq.key.length);

    lhq.replace = 1;
    lhq.value = (void *) prop;
    lhq.proto = &lvlhsh_proto;
    lhq.pool = r->mem_cache_pool;

    switch (nxt_lvlhsh_insert(&r->hash, &lhq)) {

    case NXT_OK:
        return NXT_OK;

    case NXT_DECLINED:
    default:
        return NXT_ERROR;
    }
}


static njs_ret_t
njs_unit_test_r_get_uri_external(njs_vm_t *vm, njs_value_t *value, void *obj,
    uintptr_t data)
{
    char *p = obj;

    nxt_str_t  *field;

    field = (nxt_str_t *) (p + data);

    return njs_string_create(vm, value, field->start, field->length, 0);
}


static njs_ret_t
njs_unit_test_r_set_uri_external(njs_vm_t *vm, void *obj, uintptr_t data,
    nxt_str_t *value)
{
    char *p = obj;

    nxt_str_t  *field;

    field = (nxt_str_t *) (p + data);

    *field = *value;

    return NXT_OK;
}


static njs_ret_t
njs_unit_test_r_get_a_external(njs_vm_t *vm, njs_value_t *value, void *obj,
    uintptr_t data)
{
    char                 buf[16];
    size_t               len;
    njs_unit_test_req_t  *r;

    r = (njs_unit_test_req_t *) obj;

    len = sprintf(buf, "%u", r->a);

    return njs_string_create(vm, value, (u_char *) buf, len, 0);
}


static njs_ret_t
njs_unit_test_r_get_b_external(njs_vm_t *vm, njs_value_t *value, void *obj,
    uintptr_t data)
{
    njs_value_number_set(value, data);

    return NJS_OK;
}


static njs_ret_t
njs_unit_test_host_external(njs_vm_t *vm, njs_value_t *value, void *obj,
    uintptr_t data)
{
    return njs_string_create(vm, value, (u_char *) "АБВГДЕЁЖЗИЙ", 22, 0);
}


static njs_ret_t
njs_unit_test_r_get_vars(njs_vm_t *vm, njs_value_t *value, void *obj,
    uintptr_t data)
{
    nxt_int_t             ret;
    nxt_str_t             *key;
    nxt_lvlhsh_query_t    lhq;
    njs_unit_test_req_t   *r;
    njs_unit_test_prop_t  *prop;

    r = (njs_unit_test_req_t *) obj;
    key = (nxt_str_t *) data;

    lhq.key = *key;
    lhq.key_hash = nxt_djb_hash(key->start, key->length);
    lhq.proto = &lvlhsh_proto;

    ret = nxt_lvlhsh_find(&r->hash, &lhq);

    prop = lhq.value;

    if (ret == NXT_OK && njs_is_valid(&prop->value)) {
        *value = prop->value;
        return NXT_OK;
    }

    njs_value_void_set(value);

    return NXT_OK;
}


static njs_ret_t
njs_unit_test_r_set_vars(njs_vm_t *vm, void *obj, uintptr_t data,
    nxt_str_t *value)
{
    nxt_int_t             ret;
    nxt_str_t             *key;
    njs_value_t           name, val;
    njs_unit_test_req_t   *r;
    njs_unit_test_prop_t  *prop;

    r = (njs_unit_test_req_t *) obj;
    key = (nxt_str_t *) data;

    if (key->length == 5 && memcmp(key->start, "error", 5) == 0) {
        njs_vm_error(vm, "cannot set 'error' prop");
        return NXT_ERROR;
    }

    njs_string_create(vm, &name, key->start, key->length, 0);
    njs_string_create(vm, &val, value->start, value->length, 0);

    prop = lvlhsh_unit_test_alloc(vm->mem_cache_pool, &name, &val);
    if (prop == NULL) {
        njs_memory_error(vm);
        return NXT_ERROR;
    }

    ret = lvlhsh_unit_test_add(r, prop);
    if (ret != NXT_OK) {
        njs_vm_error(vm, "lvlhsh_unit_test_add() failed");
        return NXT_ERROR;
    }

    return NXT_OK;
}


static njs_ret_t
njs_unit_test_r_del_vars(njs_vm_t *vm, void *obj, uintptr_t data,
    nxt_bool_t delete)
{
    nxt_int_t             ret;
    nxt_str_t             *key;
    nxt_lvlhsh_query_t    lhq;
    njs_unit_test_req_t   *r;
    njs_unit_test_prop_t  *prop;

    r = (njs_unit_test_req_t *) obj;
    key = (nxt_str_t *) data;

    if (key->length == 5 && memcmp(key->start, "error", 5) == 0) {
        njs_vm_error(vm, "cannot delete 'error' prop");
        return NXT_ERROR;
    }

    lhq.key = *key;
    lhq.key_hash = nxt_djb_hash(key->start, key->length);
    lhq.proto = &lvlhsh_proto;

    ret = nxt_lvlhsh_find(&r->hash, &lhq);

    prop = lhq.value;

    if (ret == NXT_OK) {
        njs_set_invalid(&prop->value);
    }

    return NXT_OK;
}


static njs_ret_t
njs_unit_test_header_external(njs_vm_t *vm, njs_value_t *value, void *obj,
    uintptr_t data)
{
    u_char     *p;
    uint32_t   size;
    nxt_str_t  *h;

    h = (nxt_str_t *) data;

    size = 7 + h->length;

    p = njs_string_alloc(vm, value, size, 0);
    if (p == NULL) {
        return NJS_ERROR;
    }

    p = nxt_cpymem(p, h->start, h->length);
    *p++ = '|';
    memcpy(p, "АБВ", 6);

    return NJS_OK;
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
    nxt_int_t            ret;
    nxt_str_t            s;
    njs_unit_test_req_t  *r;

    r = njs_vm_external(vm, njs_arg(args, nargs, 0));
    if (nxt_slow_path(r == NULL)) {
        return NJS_ERROR;
    }

    ret = njs_vm_value_to_ext_string(vm, &s, njs_arg(args, nargs, 1), 0);
    if (ret == NXT_OK && s.length == 3 && memcmp(s.start, "YES", 3) == 0) {
        return njs_string_create(vm, njs_vm_retval(vm), r->uri.start,
                                 r->uri.length, 0);
    }

    vm->retval = njs_value_void;

    return NJS_OK;
}


static njs_ret_t
njs_unit_test_create_external(njs_vm_t *vm, njs_value_t *args, nxt_uint_t nargs,
    njs_index_t unused)
{
    nxt_int_t            ret;
    nxt_str_t            uri;
    njs_value_t          *value;
    njs_unit_test_req_t  *r, *sr;

    r = njs_vm_external(vm, njs_arg(args, nargs, 0));
    if (nxt_slow_path(r == NULL)) {
        return NJS_ERROR;
    }

    if (njs_vm_value_to_ext_string(vm, &uri, njs_arg(args, nargs, 1), 0)
        != NJS_OK)
    {
        return NJS_ERROR;
    }

    value = nxt_mem_cache_zalloc(r->mem_cache_pool, sizeof(njs_opaque_value_t));
    if (value == NULL) {
        goto memory_error;
    }

    sr = nxt_mem_cache_zalloc(r->mem_cache_pool, sizeof(njs_unit_test_req_t));
    if (sr == NULL) {
        goto memory_error;
    }

    sr->uri = uri;
    sr->mem_cache_pool = r->mem_cache_pool;
    sr->proto = r->proto;

    ret = njs_vm_external_create(vm, value, sr->proto, sr);
    if (ret != NXT_OK) {
        return NJS_ERROR;
    }

    njs_vm_retval_set(vm, value);

    return NJS_OK;

memory_error:

    njs_memory_error(vm);

    return NXT_ERROR;
}


static njs_external_t  njs_unit_test_r_props[] = {

    { nxt_string("a"),
      NJS_EXTERN_PROPERTY,
      NULL,
      0,
      njs_unit_test_r_get_a_external,
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      0 },

    { nxt_string("b"),
      NJS_EXTERN_PROPERTY,
      NULL,
      0,
      njs_unit_test_r_get_b_external,
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      42 },
};


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
      offsetof(njs_unit_test_req_t, uri) },

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

    { nxt_string("props"),
      NJS_EXTERN_OBJECT,
      njs_unit_test_r_props,
      nxt_nitems(njs_unit_test_r_props),
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      0 },

    { nxt_string("vars"),
      NJS_EXTERN_OBJECT,
      NULL,
      0,
      njs_unit_test_r_get_vars,
      njs_unit_test_r_set_vars,
      njs_unit_test_r_del_vars,
      NULL,
      NULL,
      NULL,
      0 },

    { nxt_string("consts"),
      NJS_EXTERN_OBJECT,
      NULL,
      0,
      njs_unit_test_r_get_vars,
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

    { nxt_string("create"),
      NJS_EXTERN_METHOD,
      NULL,
      0,
      NULL,
      NULL,
      NULL,
      NULL,
      NULL,
      njs_unit_test_create_external,
      0 },

};


static njs_external_t  nxt_test_external[] = {

    { nxt_string("request.proto"),
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


typedef struct {
    nxt_str_t            name;
    njs_unit_test_req_t  request;
    njs_unit_test_prop_t props[2];
} njs_unit_test_req_t_init_t;


static const njs_unit_test_req_t_init_t nxt_test_requests[] = {

    { nxt_string("$r"),
     {
         .uri = nxt_string("АБВ"),
         .a = 1
     },
     {
         { njs_string("p"), njs_string("pval") },
         { njs_string("p2"), njs_string("p2val") },
     }
    },

    { nxt_string("$r2"),
     {
         .uri = nxt_string("αβγ"),
         .a = 2
     },
     {
         { njs_string("q"), njs_string("qval") },
         { njs_string("q2"), njs_string("q2val") },
     }
    },

    { nxt_string("$r3"),
     {
         .uri = nxt_string("abc"),
         .a = 3
     },
     {
         { njs_string("k"), njs_string("kval") },
         { njs_string("k2"), njs_string("k2val") },
     }
    },
};


static nxt_int_t
njs_externals_init(njs_vm_t *vm)
{
    nxt_int_t             ret;
    nxt_uint_t            i, j;
    const njs_extern_t    *proto;
    njs_unit_test_req_t   *requests;
    njs_unit_test_prop_t  *prop;

    proto = njs_vm_external_prototype(vm, &nxt_test_external[0]);
    if (proto == NULL) {
        printf("njs_vm_external_prototype() failed\n");
        return NXT_ERROR;
    }

    requests = nxt_mem_cache_zalloc(vm->mem_cache_pool,
                                    nxt_nitems(nxt_test_requests)
                                    * sizeof(njs_unit_test_req_t));
    if (requests == NULL) {
        return NXT_ERROR;
    }

    for (i = 0; i < nxt_nitems(nxt_test_requests); i++) {

        requests[i] = nxt_test_requests[i].request;
        requests[i].mem_cache_pool = vm->mem_cache_pool;
        requests[i].proto = proto;

        ret = njs_vm_external_create(vm, njs_value_arg(&requests[i].value),
                                     proto, &requests[i]);
        if (ret != NXT_OK) {
            printf("njs_vm_external_create() failed\n");
            return NXT_ERROR;
        }

        ret = njs_vm_external_bind(vm, &nxt_test_requests[i].name,
                                   njs_value_arg(&requests[i].value));
        if (ret != NXT_OK) {
            printf("njs_vm_external_bind() failed\n");
            return NXT_ERROR;
        }

        for (j = 0; j < nxt_nitems(nxt_test_requests[i].props); j++) {
            prop = lvlhsh_unit_test_alloc(vm->mem_cache_pool,
                                          &nxt_test_requests[i].props[j].name,
                                          &nxt_test_requests[i].props[j].value);

            if (prop == NULL) {
                printf("lvlhsh_unit_test_alloc() failed\n");
                return NXT_ERROR;
            }

            ret = lvlhsh_unit_test_add(&requests[i], prop);
            if (ret != NXT_OK) {
                printf("lvlhsh_unit_test_add() failed\n");
                return NXT_ERROR;
            }
        }
    }

    return NXT_OK;
}


static nxt_int_t
njs_unit_test(njs_unit_test_t tests[], size_t num, nxt_bool_t disassemble,
    nxt_bool_t verbose)
{
    u_char        *start;
    njs_vm_t      *vm, *nvm;
    nxt_int_t     ret, rc;
    nxt_str_t     s;
    nxt_uint_t    i;
    nxt_bool_t    success;
    njs_vm_opt_t  options;

    vm = NULL;
    nvm = NULL;

    rc = NXT_ERROR;

    for (i = 0; i < num; i++) {

        if (verbose) {
            printf("\"%.*s\"\n",
                   (int) njs_test[i].script.length, njs_test[i].script.start);
            fflush(stdout);
        }

        nxt_memzero(&options, sizeof(njs_vm_opt_t));

        vm = njs_vm_create(&options);
        if (vm == NULL) {
            printf("njs_vm_create() failed\n");
            goto done;
        }

        ret = njs_externals_init(vm);
        if (ret != NXT_OK) {
            goto done;
        }

        start = njs_test[i].script.start;

        ret = njs_vm_compile(vm, &start, start + njs_test[i].script.length);

        if (ret == NXT_OK) {
            if (disassemble) {
                njs_disassembler(vm);
                fflush(stdout);
            }

            nvm = njs_vm_clone(vm, NULL);
            if (nvm == NULL) {
                printf("njs_vm_clone() failed\n");
                goto done;
            }

            ret = njs_vm_run(nvm);

            if (njs_vm_retval_to_ext_string(nvm, &s) != NXT_OK) {
                printf("njs_vm_retval_to_ext_string() failed\n");
                goto done;
            }

        } else {
            if (njs_vm_retval_to_ext_string(vm, &s) != NXT_OK) {
                printf("njs_vm_retval_to_ext_string() failed\n");
                goto done;
            }
        }

        success = nxt_strstr_eq(&njs_test[i].ret, &s);

        if (success) {
            if (nvm != NULL) {
                njs_vm_destroy(nvm);
                nvm = NULL;
            }

            njs_vm_destroy(vm);
            vm = NULL;

            continue;
        }

        printf("njs(\"%.*s\")\nexpected: \"%.*s\"\n     got: \"%.*s\"\n",
               (int) njs_test[i].script.length, njs_test[i].script.start,
               (int) njs_test[i].ret.length, njs_test[i].ret.start,
               (int) s.length, s.start);

        goto done;
    }

    rc = NXT_OK;

done:

    if (nvm != NULL) {
        njs_vm_destroy(nvm);
    }

    if (vm != NULL) {
        njs_vm_destroy(vm);
    }

    return rc;
}


static nxt_int_t
njs_vm_object_alloc_test(njs_vm_t * vm, nxt_bool_t disassemble,
    nxt_bool_t verbose)
{
    njs_ret_t    ret;
    njs_value_t  args[2], obj;

    static const njs_value_t num_key = njs_string("num");
    static const njs_value_t bool_key = njs_string("bool");

    njs_value_number_set(njs_argument(&args, 0), 1);
    njs_value_boolean_set(njs_argument(&args, 1), 0);

    ret = njs_vm_object_alloc(vm, &obj, NULL);
    if (ret != NJS_OK) {
        return NXT_ERROR;
    }

    ret = njs_vm_object_alloc(vm, &obj, &num_key, NULL);
    if (ret == NJS_OK) {
        return NXT_ERROR;
    }

    ret = njs_vm_object_alloc(vm, &obj, &num_key, &args[0], NULL);
    if (ret != NJS_OK) {
        return NXT_ERROR;
    }

    ret = njs_vm_object_alloc(vm, &obj, &num_key, &args[0], &bool_key, &args[1],
                              NULL);
    if (ret != NJS_OK) {
        return NXT_ERROR;
    }

    return NXT_OK;
}


typedef struct {
    nxt_int_t  (*test)(njs_vm_t *, nxt_bool_t, nxt_bool_t);
    nxt_str_t  name;
} njs_api_test_t;


static nxt_int_t
njs_api_test(nxt_bool_t disassemble, nxt_bool_t verbose)
{
    njs_vm_t        *vm;
    nxt_int_t       ret, rc;
    nxt_uint_t      i;
    njs_vm_opt_t    options;
    njs_api_test_t  *test;

    static njs_api_test_t  njs_api_test[] =
    {
        { njs_vm_object_alloc_test,
          nxt_string("njs_vm_object_alloc_test") }
    };

    rc = NXT_ERROR;

    vm = NULL;
    nxt_memzero(&options, sizeof(njs_vm_opt_t));

    for (i = 0; i < nxt_nitems(njs_api_test); i++) {
        test = &njs_api_test[i];

        vm = njs_vm_create(&options);
        if (vm == NULL) {
            printf("njs_vm_create() failed\n");
            goto done;
        }

        ret = test->test(vm, disassemble, verbose);
        if (nxt_slow_path(ret != NXT_OK)) {
            printf("njs_api_test: \"%.*s\" test failed\n",
                   (int) test->name.length, test->name.start);
            goto done;
        }
    }

    rc = NXT_OK;

done:

    if (vm != NULL) {
        njs_vm_destroy(vm);
    }

    return rc;
}


int nxt_cdecl
main(int argc, char **argv)
{
    size_t      size;
    u_char      buf[16];
    time_t      clock;
    struct tm   tm;
    nxt_int_t   ret;
    nxt_bool_t  disassemble, verbose;

    disassemble = 0;
    verbose = 0;

    if (argc > 1) {
        switch (argv[1][0]) {

        case 'd':
            disassemble = 1;
            break;

        case 'v':
            verbose = 1;
            break;

        default:
            break;
        }
    }

    (void) putenv((char *) "TZ=UTC");
    tzset();

    ret = njs_unit_test(njs_test, nxt_nitems(njs_test), disassemble, verbose);
    if (ret != NXT_OK) {
        return ret;
    }

    printf("njs unit tests passed\n");

    /*
     * Chatham Islands NZ-CHAT time zone.
     * Standard time: UTC+12:45, Daylight Saving time: UTC+13:45.
     */
    (void) putenv((char *) "TZ=Pacific/Chatham");
    tzset();

    clock = 0;
    localtime_r(&clock, &tm);

    size = strftime((char *) buf, sizeof(buf), "%z", &tm);

    if (memcmp(buf, "+1245", size) == 0) {
        ret = njs_unit_test(njs_tz_test, nxt_nitems(njs_tz_test), disassemble,
                            verbose);
        if (ret != NXT_OK) {
            return ret;
        }

        printf("njs timezone tests passed\n");

    } else {
        printf("njs timezone tests skipped, timezone is unavailable\n");
    }


    return njs_api_test(disassemble, verbose);
}
