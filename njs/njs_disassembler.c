
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <nxt_types.h>
#include <nxt_clang.h>
#include <nxt_stub.h>
#include <nxt_array.h>
#include <nxt_lvlhsh.h>
#include <nxt_mem_cache_pool.h>
#include <njscript.h>
#include <njs_vm.h>
#include <njs_regexp.h>
#include <njs_variable.h>
#include <njs_parser.h>


typedef struct {
    njs_vmcode_operation_t     operation;
    size_t                     size;
    nxt_str_t                  name;
} njs_code_name_t;


static njs_code_name_t  code_names[] = {

    { njs_vmcode_object_create, sizeof(njs_vmcode_object_t),
          nxt_string("OBJECT CREATE   ") },
    { njs_vmcode_array_create, sizeof(njs_vmcode_array_t),
          nxt_string("ARRAY CREATE    ") },
    { njs_vmcode_function_create, sizeof(njs_vmcode_function_create_t),
          nxt_string("FUNCTION CREATE ") },
    { njs_vmcode_regexp_create, sizeof(njs_vmcode_regexp_t),
          nxt_string("REGEXP CREATE   ") },

    { njs_vmcode_property_get, sizeof(njs_vmcode_prop_get_t),
          nxt_string("PROPERTY GET    ") },
    { njs_vmcode_property_set, sizeof(njs_vmcode_prop_set_t),
          nxt_string("PROPERTY SET    ") },
    { njs_vmcode_property_in, sizeof(njs_vmcode_3addr_t),
          nxt_string("PROPERTY IN     ") },
    { njs_vmcode_property_delete, sizeof(njs_vmcode_3addr_t),
          nxt_string("PROPERTY DELETE ") },
    { njs_vmcode_property_each_start, sizeof(njs_vmcode_prop_start_t),
          nxt_string("PROPERTY START  ") },
    { njs_vmcode_property_each, sizeof(njs_vmcode_prop_each_t),
          nxt_string("PROPERTY EACH   ") },
    { njs_vmcode_instance_of, sizeof(njs_vmcode_instance_of_t),
          nxt_string("INSTANCE OF     ") },

    { njs_vmcode_function, sizeof(njs_vmcode_function_t),
          nxt_string("FUNCTION        ") },
    { njs_vmcode_method, sizeof(njs_vmcode_method_t),
          nxt_string("METHOD          ") },
    { njs_vmcode_call, sizeof(njs_vmcode_call_t),
          nxt_string("CALL            ") },
    { njs_vmcode_return, sizeof(njs_vmcode_stop_t),
          nxt_string("RETURN          ") },

    { njs_vmcode_increment, sizeof(njs_vmcode_3addr_t),
          nxt_string("INC             ") },
    { njs_vmcode_decrement, sizeof(njs_vmcode_3addr_t),
          nxt_string("DEC             ") },
    { njs_vmcode_post_increment, sizeof(njs_vmcode_3addr_t),
          nxt_string("POST INC        ") },
    { njs_vmcode_post_decrement, sizeof(njs_vmcode_3addr_t),
          nxt_string("POST DEC        ") },

    { njs_vmcode_delete, sizeof(njs_vmcode_2addr_t),
          nxt_string("DELETE          ") },
    { njs_vmcode_void, sizeof(njs_vmcode_2addr_t),
          nxt_string("VOID            ") },
    { njs_vmcode_typeof, sizeof(njs_vmcode_2addr_t),
          nxt_string("TYPEOF          ") },

    { njs_vmcode_unary_plus, sizeof(njs_vmcode_2addr_t),
          nxt_string("PLUS            ") },
    { njs_vmcode_unary_negation, sizeof(njs_vmcode_2addr_t),
          nxt_string("NEGATION        ") },

    { njs_vmcode_addition, sizeof(njs_vmcode_3addr_t),
          nxt_string("ADD             ") },
    { njs_vmcode_substraction, sizeof(njs_vmcode_3addr_t),
          nxt_string("SUBSTRACT       ") },
    { njs_vmcode_multiplication, sizeof(njs_vmcode_3addr_t),
          nxt_string("MULTIPLY        ") },
    { njs_vmcode_division, sizeof(njs_vmcode_3addr_t),
          nxt_string("DIVIDE          ") },
    { njs_vmcode_remainder, sizeof(njs_vmcode_3addr_t),
          nxt_string("REMAINDER       ") },

    { njs_vmcode_left_shift, sizeof(njs_vmcode_3addr_t),
          nxt_string("LEFT SHIFT      ") },
    { njs_vmcode_right_shift, sizeof(njs_vmcode_3addr_t),
          nxt_string("RIGHT SHIFT     ") },
    { njs_vmcode_unsigned_right_shift, sizeof(njs_vmcode_3addr_t),
          nxt_string("UNS RIGHT SHIFT ") },

    { njs_vmcode_logical_not, sizeof(njs_vmcode_2addr_t),
          nxt_string("LOGICAL NOT     ") },
    { njs_vmcode_logical_and, sizeof(njs_vmcode_3addr_t),
          nxt_string("LOGICAL AND     ") },
    { njs_vmcode_logical_or, sizeof(njs_vmcode_3addr_t),
          nxt_string("LOGICAL OR      ") },

    { njs_vmcode_bitwise_not, sizeof(njs_vmcode_2addr_t),
          nxt_string("BINARY NOT      ") },
    { njs_vmcode_bitwise_and, sizeof(njs_vmcode_3addr_t),
          nxt_string("BINARY AND      ") },
    { njs_vmcode_bitwise_xor, sizeof(njs_vmcode_3addr_t),
          nxt_string("BINARY XOR      ") },
    { njs_vmcode_bitwise_or, sizeof(njs_vmcode_3addr_t),
          nxt_string("BINARY OR       ") },

    { njs_vmcode_equal, sizeof(njs_vmcode_3addr_t),
          nxt_string("EQUAL           ") },
    { njs_vmcode_not_equal, sizeof(njs_vmcode_3addr_t),
          nxt_string("NOT EQUAL       ") },
    { njs_vmcode_less, sizeof(njs_vmcode_3addr_t),
          nxt_string("LESS            ") },
    { njs_vmcode_less_or_equal, sizeof(njs_vmcode_3addr_t),
          nxt_string("LESS OR EQUAL   ") },
    { njs_vmcode_greater, sizeof(njs_vmcode_3addr_t),
          nxt_string("GREATER         ") },
    { njs_vmcode_greater_or_equal, sizeof(njs_vmcode_3addr_t),
          nxt_string("GREATER OR EQUAL") },

    { njs_vmcode_strict_equal, sizeof(njs_vmcode_3addr_t),
          nxt_string("STRICT EQUAL    ") },
    { njs_vmcode_strict_not_equal, sizeof(njs_vmcode_3addr_t),
          nxt_string("STRICT NOT EQUAL") },

    { njs_vmcode_move, sizeof(njs_vmcode_move_t),
          nxt_string("MOVE            ") },
    { njs_vmcode_validate, sizeof(njs_vmcode_validate_t),
          nxt_string("VALIDATE        ") },

    { njs_vmcode_if_true_jump, sizeof(njs_vmcode_cond_jump_t),
          nxt_string("JUMP IF TRUE    ") },
    { njs_vmcode_if_false_jump, sizeof(njs_vmcode_cond_jump_t),
          nxt_string("JUMP IF FALSE   ") },
    { njs_vmcode_jump, sizeof(njs_vmcode_jump_t),
          nxt_string("JUMP            ") },
    { njs_vmcode_stop, sizeof(njs_vmcode_stop_t),
          nxt_string("STOP            ") },

    { njs_vmcode_try_start, sizeof(njs_vmcode_try_start_t),
          nxt_string("TRY START       ") },
    { njs_vmcode_try_end, sizeof(njs_vmcode_try_end_t),
          nxt_string("TRY END         ") },
    { njs_vmcode_throw, sizeof(njs_vmcode_throw_t),
          nxt_string("THROW           ") },
    { njs_vmcode_catch, sizeof(njs_vmcode_catch_t),
          nxt_string("CATCH           ") },
    { njs_vmcode_finally, sizeof(njs_vmcode_finally_t),
          nxt_string("FINALLY         ") },

};


void
njs_disassembler(u_char *start, u_char *end, nxt_str_t *text)
{
    u_char                  *p;
    nxt_uint_t               n;
    nxt_str_t                *name;
    njs_vmcode_1addr_t      *code1;
    njs_vmcode_2addr_t      *code2;
    njs_vmcode_3addr_t      *code3;
    njs_vmcode_method_t     *method;
    njs_code_name_t         *code_name;
    njs_vmcode_operation_t  operation;

    static nxt_str_t         unknown = nxt_string("UNKOWN");

    (void) name;
    (void) code1;
    (void) code2;
    (void) code3;
    (void) method;

    p = start;

    while (p < end) {
        operation = *(njs_vmcode_operation_t *) p;
        code_name = code_names;
        n = nxt_nitems(code_names);

        do {
             if (operation == code_name->operation) {
                 name = &code_name->name;

                 if (code_name->size == sizeof(njs_vmcode_method_t)) {
                     method = (njs_vmcode_method_t *) p;
                     nxt_log_error(NXT_LOG_INFO, log, "%V  %p %p %p %p",
                                   name, method->function, method->object,
                                   method->method, method->code.nargs);

                 } else if (code_name->size == sizeof(njs_vmcode_3addr_t)) {
                     code3 = (njs_vmcode_3addr_t *) p;
                     nxt_log_error(NXT_LOG_INFO, log, "%V  %p %p %p",
                                   name, code3->dst, code3->src1, code3->src2);

                 } else if (code_name->size == sizeof(njs_vmcode_2addr_t)) {
                     code2 = (njs_vmcode_2addr_t *) p;
                     nxt_log_error(NXT_LOG_INFO, log, "%V  %p %p",
                                   name, code2->dst, code2->src);

                 } else if (code_name->size == sizeof(njs_vmcode_1addr_t)) {
                     code1 = (njs_vmcode_1addr_t *) p;
                     nxt_log_error(NXT_LOG_INFO, log, "%V  %p",
                                   name, code1->index);
                 }

                 p += code_name->size;

                 goto next;
             }

             code_name++;
             n--;

        } while (n != 0);

        p += sizeof(njs_vmcode_operation_t);
        name = &unknown;

        nxt_log_error(NXT_LOG_INFO, log, "%V  %p", name, operation);

    next:

        continue;
    }
}
