
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <njs_core.h>
#include <string.h>


typedef struct njs_lexer_multi_s  njs_lexer_multi_t;

struct njs_lexer_multi_s {
    uint8_t                  symbol;
    uint8_t                  token;
    uint8_t                  count;
    const njs_lexer_multi_t  *next;
};


static njs_token_t njs_lexer_next_token(njs_lexer_t *lexer);
static njs_token_t njs_lexer_word(njs_lexer_t *lexer, u_char c);
static njs_token_t njs_lexer_string(njs_lexer_t *lexer, u_char quote);
static njs_token_t njs_lexer_number(njs_lexer_t *lexer, u_char c);
static njs_token_t njs_lexer_multi(njs_lexer_t *lexer,
    njs_token_t token, nxt_uint_t n, const njs_lexer_multi_t *multi);
static njs_token_t njs_lexer_division(njs_lexer_t *lexer,
    njs_token_t token);


static const uint8_t  njs_tokens[256]  nxt_aligned(64) = {

                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
    /* \t */    NJS_TOKEN_ILLEGAL,           NJS_TOKEN_SPACE,
    /* \n */    NJS_TOKEN_LINE_END,          NJS_TOKEN_ILLEGAL,
    /* \r */    NJS_TOKEN_ILLEGAL,           NJS_TOKEN_SPACE,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,

    /* 0x10 */  NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,

    /*   ! */   NJS_TOKEN_SPACE,             NJS_TOKEN_LOGICAL_NOT,
    /* " # */   NJS_TOKEN_DOUBLE_QUOTE,      NJS_TOKEN_ILLEGAL,
    /* $ % */   NJS_TOKEN_LETTER,            NJS_TOKEN_REMAINDER,
    /* & ' */   NJS_TOKEN_BITWISE_AND,       NJS_TOKEN_SINGLE_QUOTE,
    /* ( ) */   NJS_TOKEN_OPEN_PARENTHESIS,  NJS_TOKEN_CLOSE_PARENTHESIS,
    /* * + */   NJS_TOKEN_MULTIPLICATION,    NJS_TOKEN_ADDITION,
    /* , - */   NJS_TOKEN_COMMA,             NJS_TOKEN_SUBSTRACTION,
    /* . / */   NJS_TOKEN_DOT,               NJS_TOKEN_DIVISION,

    /* 0 1 */   NJS_TOKEN_DIGIT,             NJS_TOKEN_DIGIT,
    /* 2 3 */   NJS_TOKEN_DIGIT,             NJS_TOKEN_DIGIT,
    /* 4 5 */   NJS_TOKEN_DIGIT,             NJS_TOKEN_DIGIT,
    /* 6 7 */   NJS_TOKEN_DIGIT,             NJS_TOKEN_DIGIT,
    /* 8 9 */   NJS_TOKEN_DIGIT,             NJS_TOKEN_DIGIT,
    /* : ; */   NJS_TOKEN_COLON,             NJS_TOKEN_SEMICOLON,
    /* < = */   NJS_TOKEN_LESS,              NJS_TOKEN_ASSIGNMENT,
    /* > ? */   NJS_TOKEN_GREATER,           NJS_TOKEN_CONDITIONAL,

    /* @ A */   NJS_TOKEN_ILLEGAL,           NJS_TOKEN_LETTER,
    /* B C */   NJS_TOKEN_LETTER,            NJS_TOKEN_LETTER,
    /* D E */   NJS_TOKEN_LETTER,            NJS_TOKEN_LETTER,
    /* F G */   NJS_TOKEN_LETTER,            NJS_TOKEN_LETTER,
    /* H I */   NJS_TOKEN_LETTER,            NJS_TOKEN_LETTER,
    /* J K */   NJS_TOKEN_LETTER,            NJS_TOKEN_LETTER,
    /* L M */   NJS_TOKEN_LETTER,            NJS_TOKEN_LETTER,
    /* N O */   NJS_TOKEN_LETTER,            NJS_TOKEN_LETTER,

    /* P Q */   NJS_TOKEN_LETTER,            NJS_TOKEN_LETTER,
    /* R S */   NJS_TOKEN_LETTER,            NJS_TOKEN_LETTER,
    /* T U */   NJS_TOKEN_LETTER,            NJS_TOKEN_LETTER,
    /* V W */   NJS_TOKEN_LETTER,            NJS_TOKEN_LETTER,
    /* X Y */   NJS_TOKEN_LETTER,            NJS_TOKEN_LETTER,
    /* Z [ */   NJS_TOKEN_LETTER,            NJS_TOKEN_OPEN_BRACKET,
    /* \ ] */   NJS_TOKEN_ILLEGAL,           NJS_TOKEN_CLOSE_BRACKET,
    /* ^ _ */   NJS_TOKEN_BITWISE_XOR,       NJS_TOKEN_LETTER,

    /* ` a */   NJS_TOKEN_ILLEGAL,           NJS_TOKEN_LETTER,
    /* b c */   NJS_TOKEN_LETTER,            NJS_TOKEN_LETTER,
    /* d e */   NJS_TOKEN_LETTER,            NJS_TOKEN_LETTER,
    /* f g */   NJS_TOKEN_LETTER,            NJS_TOKEN_LETTER,
    /* h i */   NJS_TOKEN_LETTER,            NJS_TOKEN_LETTER,
    /* j k */   NJS_TOKEN_LETTER,            NJS_TOKEN_LETTER,
    /* l m */   NJS_TOKEN_LETTER,            NJS_TOKEN_LETTER,
    /* n o */   NJS_TOKEN_LETTER,            NJS_TOKEN_LETTER,

    /* p q */   NJS_TOKEN_LETTER,            NJS_TOKEN_LETTER,
    /* r s */   NJS_TOKEN_LETTER,            NJS_TOKEN_LETTER,
    /* t u */   NJS_TOKEN_LETTER,            NJS_TOKEN_LETTER,
    /* v w */   NJS_TOKEN_LETTER,            NJS_TOKEN_LETTER,
    /* x y */   NJS_TOKEN_LETTER,            NJS_TOKEN_LETTER,
    /* z { */   NJS_TOKEN_LETTER,            NJS_TOKEN_OPEN_BRACE,
    /* | } */   NJS_TOKEN_BITWISE_OR,        NJS_TOKEN_CLOSE_BRACE,
    /* ~   */   NJS_TOKEN_BITWISE_NOT,       NJS_TOKEN_ILLEGAL,

    /* 0x80 */  NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,

    /* 0x90 */  NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,

    /* 0xA0 */  NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,

    /* 0xB0 */  NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,

    /* TODO: the first byte of valid UTF-8: 0xC2 - 0xF4. */

    /* 0xC0 */  NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,

    /* 0xD0 */  NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,

    /* 0xE0 */  NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,

    /* 0xF0 */  NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
                NJS_TOKEN_ILLEGAL,           NJS_TOKEN_ILLEGAL,
};


static const njs_lexer_multi_t  njs_addition_token[] = {
    { '+', NJS_TOKEN_INCREMENT, 0, NULL },
    { '=', NJS_TOKEN_ADDITION_ASSIGNMENT, 0, NULL },
};


static const njs_lexer_multi_t  njs_substraction_token[] = {
    { '-', NJS_TOKEN_DECREMENT, 0, NULL },
    { '=', NJS_TOKEN_SUBSTRACTION_ASSIGNMENT, 0, NULL },
};


static const njs_lexer_multi_t  njs_exponentiation_token[] = {
    { '=', NJS_TOKEN_EXPONENTIATION_ASSIGNMENT, 0, NULL },
};


static const njs_lexer_multi_t  njs_multiplication_token[] = {
    { '=', NJS_TOKEN_MULTIPLICATION_ASSIGNMENT, 0, NULL },
    { '*', NJS_TOKEN_EXPONENTIATION, 1, njs_exponentiation_token },
};


static const njs_lexer_multi_t  njs_remainder_token[] = {
    { '=', NJS_TOKEN_REMAINDER_ASSIGNMENT, 0, NULL },
};


static const njs_lexer_multi_t  njs_bitwise_and_token[] = {
    { '&', NJS_TOKEN_LOGICAL_AND, 0, NULL },
    { '=', NJS_TOKEN_BITWISE_AND_ASSIGNMENT, 0, NULL },
};


static const njs_lexer_multi_t  njs_bitwise_xor_token[] = {
    { '=', NJS_TOKEN_BITWISE_XOR_ASSIGNMENT, 0, NULL },
};


static const njs_lexer_multi_t  njs_bitwise_or_token[] = {
    { '|', NJS_TOKEN_LOGICAL_OR, 0, NULL },
    { '=', NJS_TOKEN_BITWISE_OR_ASSIGNMENT, 0, NULL },
};


static const njs_lexer_multi_t  njs_strict_not_equal_token[] = {
    { '=', NJS_TOKEN_STRICT_NOT_EQUAL, 0, NULL },
};


static const njs_lexer_multi_t  njs_logical_not_token[] = {
    { '=', NJS_TOKEN_NOT_EQUAL, 1, njs_strict_not_equal_token },
};


static const njs_lexer_multi_t  njs_less_shift_token[] = {
    { '=', NJS_TOKEN_LEFT_SHIFT_ASSIGNMENT, 0, NULL },
};


static const njs_lexer_multi_t  njs_less_token[] = {
    { '=', NJS_TOKEN_LESS_OR_EQUAL, 0, NULL },
    { '<', NJS_TOKEN_LEFT_SHIFT, 1, njs_less_shift_token },
};


static const njs_lexer_multi_t  njs_less_equal_token[] = {
    { '=', NJS_TOKEN_STRICT_EQUAL, 0, NULL },
};


static const njs_lexer_multi_t  njs_unsigned_right_shift_token[] = {
    { '=', NJS_TOKEN_UNSIGNED_RIGHT_SHIFT_ASSIGNMENT, 0, NULL },
};


static const njs_lexer_multi_t  njs_right_shift_token[] = {
    { '=', NJS_TOKEN_RIGHT_SHIFT_ASSIGNMENT, 0, NULL },
    { '>', NJS_TOKEN_UNSIGNED_RIGHT_SHIFT, 1,
           njs_unsigned_right_shift_token },
};


static const njs_lexer_multi_t  njs_greater_token[] = {
    { '=', NJS_TOKEN_GREATER_OR_EQUAL, 0, NULL },
    { '>', NJS_TOKEN_RIGHT_SHIFT, 2, njs_right_shift_token },
};


static const njs_lexer_multi_t  njs_assignment_token[] = {
    { '=', NJS_TOKEN_EQUAL, 1, njs_less_equal_token },
};


njs_token_t
njs_lexer_token(njs_lexer_t *lexer)
{
    njs_token_t  token;

    lexer->prev_start = lexer->start;
    lexer->prev_token = lexer->token;

    token = njs_lexer_next_token(lexer);

    lexer->token = token;

    return token;
}


void
njs_lexer_rollback(njs_lexer_t *lexer)
{
    lexer->start = lexer->prev_start;
}


static njs_token_t
njs_lexer_next_token(njs_lexer_t *lexer)
{
    u_char                   c, *p;
    nxt_uint_t               n;
    njs_token_t              token;
    const njs_lexer_multi_t  *multi;

    lexer->text.start = lexer->start;

    while (lexer->start < lexer->end) {
        c = *lexer->start++;

        token = njs_tokens[c];

        switch (token) {

        case NJS_TOKEN_SPACE:
            lexer->text.start = lexer->start;
            continue;

        case NJS_TOKEN_LETTER:
            return njs_lexer_word(lexer, c);

        case NJS_TOKEN_DOUBLE_QUOTE:
        case NJS_TOKEN_SINGLE_QUOTE:
            return njs_lexer_string(lexer, c);

        case NJS_TOKEN_DOT:
            p = lexer->start;

            if (p == lexer->end || njs_tokens[*p] != NJS_TOKEN_DIGIT) {
                lexer->text.length = p - lexer->text.start;
                return NJS_TOKEN_DOT;
            }

            /* Fall through. */

        case NJS_TOKEN_DIGIT:
            return njs_lexer_number(lexer, c);

        case NJS_TOKEN_ASSIGNMENT:
            n = nxt_nitems(njs_assignment_token),
            multi = njs_assignment_token;

            goto multi;

        case NJS_TOKEN_ADDITION:
            n = nxt_nitems(njs_addition_token),
            multi = njs_addition_token;

            goto multi;

        case NJS_TOKEN_SUBSTRACTION:
            n = nxt_nitems(njs_substraction_token),
            multi = njs_substraction_token;

            goto multi;

        case NJS_TOKEN_MULTIPLICATION:
            n = nxt_nitems(njs_multiplication_token),
            multi = njs_multiplication_token;

            goto multi;

        case NJS_TOKEN_DIVISION:
            token = njs_lexer_division(lexer, token);

            if (token != NJS_TOKEN_AGAIN) {
                return token;
            }

            continue;

        case NJS_TOKEN_REMAINDER:
            n = nxt_nitems(njs_remainder_token),
            multi = njs_remainder_token;

            goto multi;

        case NJS_TOKEN_BITWISE_AND:
            n = nxt_nitems(njs_bitwise_and_token),
            multi = njs_bitwise_and_token;

            goto multi;

        case NJS_TOKEN_BITWISE_XOR:
            n = nxt_nitems(njs_bitwise_xor_token),
            multi = njs_bitwise_xor_token;

            goto multi;

        case NJS_TOKEN_BITWISE_OR:
            n = nxt_nitems(njs_bitwise_or_token),
            multi = njs_bitwise_or_token;

            goto multi;

        case NJS_TOKEN_LOGICAL_NOT:
            n = nxt_nitems(njs_logical_not_token),
            multi = njs_logical_not_token;

            goto multi;

        case NJS_TOKEN_LESS:
            n = nxt_nitems(njs_less_token),
            multi = njs_less_token;

            goto multi;

        case NJS_TOKEN_GREATER:
            n = nxt_nitems(njs_greater_token),
            multi = njs_greater_token;

            goto multi;

        case NJS_TOKEN_LINE_END:
            lexer->line++;

            /* Fall through. */

        case NJS_TOKEN_BITWISE_NOT:
        case NJS_TOKEN_OPEN_PARENTHESIS:
        case NJS_TOKEN_CLOSE_PARENTHESIS:
        case NJS_TOKEN_OPEN_BRACKET:
        case NJS_TOKEN_CLOSE_BRACKET:
        case NJS_TOKEN_OPEN_BRACE:
        case NJS_TOKEN_CLOSE_BRACE:
        case NJS_TOKEN_COMMA:
        case NJS_TOKEN_COLON:
        case NJS_TOKEN_SEMICOLON:
        case NJS_TOKEN_CONDITIONAL:
            lexer->text.length = lexer->start - lexer->text.start;
            return token;

        case NJS_TOKEN_ILLEGAL:
        default:
            lexer->start--;
            return token;
        }

    multi:

        return njs_lexer_multi(lexer, token, n, multi);
    }

    return NJS_TOKEN_END;
}


static njs_token_t
njs_lexer_word(njs_lexer_t *lexer, u_char c)
{
    u_char  *p;

    /* TODO: UTF-8 */

    static const uint8_t  letter_digit[32]  nxt_aligned(32) = {
        0x00, 0x00, 0x00, 0x00, /* 0000 0000 0000 0000  0000 0000 0000 0000 */

                                /* '&%$ #"!  /.-, |*)(  7654 3210 ?>=< ;:98 */
        0x10, 0x00, 0xff, 0x03, /* 0001 0000 0000 0000  1111 1111 0000 0011 */

                                /* GFED CBA@ ONML KJIH  WVUT SRQP _^]\ [ZYX */
        0xfe, 0xff, 0xff, 0x87, /* 1111 1110 1111 1111  1111 1111 1000 0111 */

                                /* gfed cba` onml kjih  wvut srqp  ~}| {zyx */
        0xfe, 0xff, 0xff, 0x07, /* 1111 1110 1111 1111  1111 1111 0000 0111 */

        0x00, 0x00, 0x00, 0x00, /* 0000 0000 0000 0000  0000 0000 0000 0000 */
        0x00, 0x00, 0x00, 0x00, /* 0000 0000 0000 0000  0000 0000 0000 0000 */
        0x00, 0x00, 0x00, 0x00, /* 0000 0000 0000 0000  0000 0000 0000 0000 */
        0x00, 0x00, 0x00, 0x00, /* 0000 0000 0000 0000  0000 0000 0000 0000 */
    };

    lexer->token_line = lexer->line;
    lexer->key_hash = nxt_djb_hash_add(NXT_DJB_HASH_INIT, c);
    lexer->text.start = lexer->start - 1;

    for (p = lexer->start; p < lexer->end; p++) {
        c = *p;

        if ((letter_digit[c / 8] & (1 << (c & 7))) == 0) {
            break;
        }

        lexer->key_hash = nxt_djb_hash_add(lexer->key_hash, c);
    }

    lexer->start = p;
    lexer->text.length = p - lexer->text.start;

    if (lexer->property) {
        return NJS_TOKEN_NAME;
    }

    return njs_lexer_keyword(lexer);
}


static njs_token_t
njs_lexer_string(njs_lexer_t *lexer, u_char quote)
{
    u_char      *p, c;
    nxt_bool_t  escape;

    escape = 0;
    lexer->text.start = lexer->start;
    p = lexer->start;

    while (p < lexer->end) {

        c = *p++;

        if (c == '\\') {
            if (p == lexer->end) {
                break;
            }

            p++;
            escape = 1;

            continue;
        }

        if (c == quote) {
            lexer->start = p;
            lexer->text.length = (p - 1) - lexer->text.start;

            if (escape == 0) {
                return NJS_TOKEN_STRING;
            }

            return NJS_TOKEN_ESCAPE_STRING;
        }
    }

    lexer->text.start--;
    lexer->text.length = p - lexer->text.start;

    return NJS_TOKEN_UNTERMINATED_STRING;
}


static njs_token_t
njs_lexer_number(njs_lexer_t *lexer, u_char c)
{
    const u_char  *p;

    lexer->text.start = lexer->start - 1;

    p = lexer->start;

    if (c == '0' && p != lexer->end) {

        /* Hexadecimal literal values. */

        if (*p == 'x' || *p == 'X') {
            p++;

            if (p == lexer->end) {
                goto illegal_token;
            }

            lexer->number = njs_number_hex_parse(&p, lexer->end);

            goto done;
        }

        /* Octal literal values. */

        if (*p == 'o' || *p == 'O') {
            p++;

            if (p == lexer->end) {
                goto illegal_token;
            }

            lexer->number = njs_number_oct_parse(&p, lexer->end);

            if (p < lexer->end && (*p == '8' || *p == '9')) {
                goto illegal_trailer;
            }

            goto done;
        }

        /* Binary literal values. */

        if (*p == 'b' || *p == 'B') {
            p++;

            if (p == lexer->end) {
                goto illegal_token;
            }

            lexer->number = njs_number_bin_parse(&p, lexer->end);

            if (p < lexer->end && (*p >= '2' && *p <= '9')) {
                goto illegal_trailer;
            }

            goto done;
        }

        /* Legacy Octal literals are deprecated. */

        if (*p >= '0' && *p <= '9') {
            goto illegal_trailer;
        }
    }

    p--;
    lexer->number = njs_number_dec_parse(&p, lexer->end);

done:

    lexer->start = (u_char *) p;
    lexer->text.length = p - lexer->text.start;

    return NJS_TOKEN_NUMBER;

illegal_trailer:

    p++;

illegal_token:

    lexer->text.length = p - lexer->text.start;

    return NJS_TOKEN_ILLEGAL;
}


static njs_token_t
njs_lexer_multi(njs_lexer_t *lexer, njs_token_t token, nxt_uint_t n,
    const njs_lexer_multi_t *multi)
{
    u_char  c;

    if (lexer->start < lexer->end) {
        c = lexer->start[0];

        do {
            if (c == multi->symbol) {
                lexer->start++;

                if (multi->count == 0) {
                    token = multi->token;
                    break;
                }

                return njs_lexer_multi(lexer, multi->token, multi->count,
                                       multi->next);
            }

            multi++;
            n--;

        } while (n != 0);
    }

    lexer->text.length = lexer->start - lexer->text.start;

    return token;
}


static njs_token_t
njs_lexer_division(njs_lexer_t *lexer, njs_token_t token)
{
    u_char  c, *p;

    if (lexer->start < lexer->end) {
        c = lexer->start[0];

        if (c == '/') {
            token = NJS_TOKEN_END;
            lexer->start++;

            for (p = lexer->start; p < lexer->end; p++) {

                if (*p == '\n') {
                    lexer->start = p + 1;
                    lexer->line++;
                    return NJS_TOKEN_LINE_END;
                }
            }

        } else if (c == '*') {
            lexer->start++;

            for (p = lexer->start; p < lexer->end; p++) {

                if (*p == '\n') {
                    lexer->line++;
                    continue;
                }

                if (*p == '*') {
                    p++;

                    if (p < lexer->end && *p == '/') {
                        lexer->start = p + 1;
                        return NJS_TOKEN_AGAIN;
                    }
                }
            }

            return NJS_TOKEN_ILLEGAL;

        } else if (c == '=') {
            lexer->start++;
            token = NJS_TOKEN_DIVISION_ASSIGNMENT;
        }
    }

    return token;
}
