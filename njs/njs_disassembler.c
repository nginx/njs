
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */

#include <njs_core.h>


static void njs_disassemble(u_char *start, u_char *end);


typedef struct {
    njs_vmcode_operation_t     operation;
    size_t                     size;
    nxt_str_t                  name;
} njs_code_name_t;


static njs_code_name_t  code_names[] = {

    { NJS_VMCODE_OBJECT, sizeof(njs_vmcode_object_t),
          nxt_string("OBJECT          ") },
    { NJS_VMCODE_FUNCTION, sizeof(njs_vmcode_function_t),
          nxt_string("FUNCTION        ") },
    { NJS_VMCODE_THIS, sizeof(njs_vmcode_this_t),
          nxt_string("THIS            ") },
    { NJS_VMCODE_ARGUMENTS, sizeof(njs_vmcode_arguments_t),
          nxt_string("ARGUMENTS       ") },
    { NJS_VMCODE_REGEXP, sizeof(njs_vmcode_regexp_t),
          nxt_string("REGEXP          ") },
    { NJS_VMCODE_TEMPLATE_LITERAL, sizeof(njs_vmcode_template_literal_t),
          nxt_string("TEMPLATE LITERAL") },
    { NJS_VMCODE_OBJECT_COPY, sizeof(njs_vmcode_object_copy_t),
          nxt_string("OBJECT COPY     ") },

    { NJS_VMCODE_PROPERTY_GET, sizeof(njs_vmcode_prop_get_t),
          nxt_string("PROPERTY GET    ") },
    { NJS_VMCODE_PROPERTY_INIT, sizeof(njs_vmcode_prop_set_t),
          nxt_string("PROPERTY INIT   ") },
    { NJS_VMCODE_PROPERTY_SET, sizeof(njs_vmcode_prop_set_t),
          nxt_string("PROPERTY SET    ") },
    { NJS_VMCODE_PROPERTY_IN, sizeof(njs_vmcode_3addr_t),
          nxt_string("PROPERTY IN     ") },
    { NJS_VMCODE_PROPERTY_DELETE, sizeof(njs_vmcode_3addr_t),
          nxt_string("PROPERTY DELETE ") },
    { NJS_VMCODE_INSTANCE_OF, sizeof(njs_vmcode_instance_of_t),
          nxt_string("INSTANCE OF     ") },

    { NJS_VMCODE_FUNCTION_CALL, sizeof(njs_vmcode_function_call_t),
          nxt_string("FUNCTION CALL   ") },
    { NJS_VMCODE_RETURN, sizeof(njs_vmcode_return_t),
          nxt_string("RETURN          ") },
    { NJS_VMCODE_STOP, sizeof(njs_vmcode_stop_t),
          nxt_string("STOP            ") },

    { NJS_VMCODE_INCREMENT, sizeof(njs_vmcode_3addr_t),
          nxt_string("INC             ") },
    { NJS_VMCODE_DECREMENT, sizeof(njs_vmcode_3addr_t),
          nxt_string("DEC             ") },
    { NJS_VMCODE_POST_INCREMENT, sizeof(njs_vmcode_3addr_t),
          nxt_string("POST INC        ") },
    { NJS_VMCODE_POST_DECREMENT, sizeof(njs_vmcode_3addr_t),
          nxt_string("POST DEC        ") },

    { NJS_VMCODE_DELETE, sizeof(njs_vmcode_2addr_t),
          nxt_string("DELETE          ") },
    { NJS_VMCODE_VOID, sizeof(njs_vmcode_2addr_t),
          nxt_string("VOID            ") },
    { NJS_VMCODE_TYPEOF, sizeof(njs_vmcode_2addr_t),
          nxt_string("TYPEOF          ") },

    { NJS_VMCODE_UNARY_PLUS, sizeof(njs_vmcode_2addr_t),
          nxt_string("PLUS            ") },
    { NJS_VMCODE_UNARY_NEGATION, sizeof(njs_vmcode_2addr_t),
          nxt_string("NEGATION        ") },

    { NJS_VMCODE_ADDITION, sizeof(njs_vmcode_3addr_t),
          nxt_string("ADD             ") },
    { NJS_VMCODE_SUBSTRACTION, sizeof(njs_vmcode_3addr_t),
          nxt_string("SUBSTRACT       ") },
    { NJS_VMCODE_MULTIPLICATION, sizeof(njs_vmcode_3addr_t),
          nxt_string("MULTIPLY        ") },
    { NJS_VMCODE_EXPONENTIATION, sizeof(njs_vmcode_3addr_t),
          nxt_string("POWER           ") },
    { NJS_VMCODE_DIVISION, sizeof(njs_vmcode_3addr_t),
          nxt_string("DIVIDE          ") },
    { NJS_VMCODE_REMAINDER, sizeof(njs_vmcode_3addr_t),
          nxt_string("REMAINDER       ") },

    { NJS_VMCODE_LEFT_SHIFT, sizeof(njs_vmcode_3addr_t),
          nxt_string("LEFT SHIFT      ") },
    { NJS_VMCODE_RIGHT_SHIFT, sizeof(njs_vmcode_3addr_t),
          nxt_string("RIGHT SHIFT     ") },
    { NJS_VMCODE_UNSIGNED_RIGHT_SHIFT, sizeof(njs_vmcode_3addr_t),
          nxt_string("USGN RIGHT SHIFT") },

    { NJS_VMCODE_LOGICAL_NOT, sizeof(njs_vmcode_2addr_t),
          nxt_string("LOGICAL NOT     ") },

    { NJS_VMCODE_BITWISE_NOT, sizeof(njs_vmcode_2addr_t),
          nxt_string("BINARY NOT      ") },
    { NJS_VMCODE_BITWISE_AND, sizeof(njs_vmcode_3addr_t),
          nxt_string("BINARY AND      ") },
    { NJS_VMCODE_BITWISE_XOR, sizeof(njs_vmcode_3addr_t),
          nxt_string("BINARY XOR      ") },
    { NJS_VMCODE_BITWISE_OR, sizeof(njs_vmcode_3addr_t),
          nxt_string("BINARY OR       ") },

    { NJS_VMCODE_EQUAL, sizeof(njs_vmcode_3addr_t),
          nxt_string("EQUAL           ") },
    { NJS_VMCODE_NOT_EQUAL, sizeof(njs_vmcode_3addr_t),
          nxt_string("NOT EQUAL       ") },
    { NJS_VMCODE_LESS, sizeof(njs_vmcode_3addr_t),
          nxt_string("LESS            ") },
    { NJS_VMCODE_LESS_OR_EQUAL, sizeof(njs_vmcode_3addr_t),
          nxt_string("LESS OR EQUAL   ") },
    { NJS_VMCODE_GREATER, sizeof(njs_vmcode_3addr_t),
          nxt_string("GREATER         ") },
    { NJS_VMCODE_GREATER_OR_EQUAL, sizeof(njs_vmcode_3addr_t),
          nxt_string("GREATER OR EQUAL") },

    { NJS_VMCODE_STRICT_EQUAL, sizeof(njs_vmcode_3addr_t),
          nxt_string("STRICT EQUAL    ") },
    { NJS_VMCODE_STRICT_NOT_EQUAL, sizeof(njs_vmcode_3addr_t),
          nxt_string("STRICT NOT EQUAL") },

    { NJS_VMCODE_MOVE, sizeof(njs_vmcode_move_t),
          nxt_string("MOVE            ") },

    { NJS_VMCODE_THROW, sizeof(njs_vmcode_throw_t),
          nxt_string("THROW           ") },

};


void
njs_disassembler(njs_vm_t *vm)
{
    nxt_uint_t     n;
    njs_vm_code_t  *code;

    code = vm->codes->start;
    n = vm->codes->items;

    while (n != 0) {
        nxt_printf("%V:%V\n", &code->file, &code->name);
        njs_disassemble(code->start, code->end);
        code++;
        n--;
    }
}


static void
njs_disassemble(u_char *start, u_char *end)
{
    u_char                       *p;
    nxt_str_t                    *name;
    nxt_uint_t                   n;
    const char                   *sign;
    njs_code_name_t              *code_name;
    njs_vmcode_jump_t            *jump;
    njs_vmcode_1addr_t           *code1;
    njs_vmcode_2addr_t           *code2;
    njs_vmcode_3addr_t           *code3;
    njs_vmcode_array_t           *array;
    njs_vmcode_catch_t           *catch;
    njs_vmcode_finally_t         *finally;
    njs_vmcode_try_end_t         *try_end;
    njs_vmcode_try_start_t       *try_start;
    njs_vmcode_operation_t       operation;
    njs_vmcode_cond_jump_t       *cond_jump;
    njs_vmcode_test_jump_t       *test_jump;
    njs_vmcode_prop_next_t       *prop_next;
    njs_vmcode_try_return_t      *try_return;
    njs_vmcode_equal_jump_t      *equal;
    njs_vmcode_prop_foreach_t    *prop_foreach;
    njs_vmcode_method_frame_t    *method;
    njs_vmcode_try_trampoline_t  *try_tramp;
    njs_vmcode_function_frame_t  *function;

    p = start;

    /*
     * On some 32-bit platform uintptr_t is int and compilers warn
     * about %l format modifier.  size_t has the size as pointer so
     * there is no run-time overhead.
     */

    while (p < end) {
        operation = *(njs_vmcode_operation_t *) p;

        if (operation == NJS_VMCODE_ARRAY) {
            array = (njs_vmcode_array_t *) p;

            nxt_printf("%05uz ARRAY             %04Xz %uz%s\n",
                       p - start, (size_t) array->retval,
                       (size_t) array->length, array->code.ctor ? " INIT" : "");

            p += sizeof(njs_vmcode_array_t);

            continue;
        }

        if (operation == NJS_VMCODE_IF_TRUE_JUMP) {
            cond_jump = (njs_vmcode_cond_jump_t *) p;
            sign = (cond_jump->offset >= 0) ? "+" : "";

            nxt_printf("%05uz JUMP IF TRUE      %04Xz %s%uz\n",
                       p - start, (size_t) cond_jump->cond, sign,
                       (size_t) cond_jump->offset);

            p += sizeof(njs_vmcode_cond_jump_t);

            continue;
        }

        if (operation == NJS_VMCODE_IF_FALSE_JUMP) {
            cond_jump = (njs_vmcode_cond_jump_t *) p;
            sign = (cond_jump->offset >= 0) ? "+" : "";

            nxt_printf("%05uz JUMP IF FALSE     %04Xz %s%uz\n",
                       p - start, (size_t) cond_jump->cond, sign,
                       (size_t) cond_jump->offset);

            p += sizeof(njs_vmcode_cond_jump_t);

            continue;
        }

        if (operation == NJS_VMCODE_JUMP) {
            jump = (njs_vmcode_jump_t *) p;
            sign = (jump->offset >= 0) ? "+" : "";

            nxt_printf("%05uz JUMP              %s%uz\n",
                       p - start, sign, (size_t) jump->offset);

            p += sizeof(njs_vmcode_jump_t);

            continue;
        }

        if (operation == NJS_VMCODE_IF_EQUAL_JUMP) {
            equal = (njs_vmcode_equal_jump_t *) p;

            nxt_printf("%05uz JUMP IF EQUAL     %04Xz %04Xz +%uz\n",
                       p - start, (size_t) equal->value1,
                       (size_t) equal->value2, (size_t) equal->offset);

            p += sizeof(njs_vmcode_equal_jump_t);

            continue;
        }

        if (operation == NJS_VMCODE_TEST_IF_TRUE) {
            test_jump = (njs_vmcode_test_jump_t *) p;

            nxt_printf("%05uz TEST IF TRUE      %04Xz %04Xz +%uz\n",
                       p - start, (size_t) test_jump->retval,
                       (size_t) test_jump->value, (size_t) test_jump->offset);

            p += sizeof(njs_vmcode_test_jump_t);

            continue;
        }

        if (operation == NJS_VMCODE_TEST_IF_FALSE) {
            test_jump = (njs_vmcode_test_jump_t *) p;

            nxt_printf("%05uz TEST IF FALSE     %04Xz %04Xz +%uz\n",
                       p - start, (size_t) test_jump->retval,
                       (size_t) test_jump->value, (size_t) test_jump->offset);

            p += sizeof(njs_vmcode_test_jump_t);

            continue;
        }

        if (operation == NJS_VMCODE_FUNCTION_FRAME) {
            function = (njs_vmcode_function_frame_t *) p;

            nxt_printf("%05uz FUNCTION FRAME    %04Xz %uz%s\n",
                       p - start, (size_t) function->name, function->nargs,
                       function->code.ctor ? " CTOR" : "");

            p += sizeof(njs_vmcode_function_frame_t);

            continue;
        }

        if (operation == NJS_VMCODE_METHOD_FRAME) {
            method = (njs_vmcode_method_frame_t *) p;

            nxt_printf("%05uz METHOD FRAME      %04Xz %04Xz %uz%s\n",
                       p - start, (size_t) method->object,
                       (size_t) method->method, method->nargs,
                       method->code.ctor ? " CTOR" : "");

            p += sizeof(njs_vmcode_method_frame_t);
            continue;
        }

        if (operation == NJS_VMCODE_PROPERTY_FOREACH) {
            prop_foreach = (njs_vmcode_prop_foreach_t *) p;

            nxt_printf("%05uz PROPERTY FOREACH  %04Xz %04Xz +%uz\n",
                       p - start, (size_t) prop_foreach->next,
                       (size_t) prop_foreach->object,
                       (size_t) prop_foreach->offset);

            p += sizeof(njs_vmcode_prop_foreach_t);
            continue;
        }

        if (operation == NJS_VMCODE_PROPERTY_NEXT) {
            prop_next = (njs_vmcode_prop_next_t *) p;

            nxt_printf("%05uz PROPERTY NEXT     %04Xz %04Xz %04Xz %uz\n",
                       p - start, (size_t) prop_next->retval,
                       (size_t) prop_next->object, (size_t) prop_next->next,
                       (size_t) prop_next->offset);

            p += sizeof(njs_vmcode_prop_next_t);

            continue;
        }

        if (operation == NJS_VMCODE_TRY_START) {
            try_start = (njs_vmcode_try_start_t *) p;

            nxt_printf("%05uz TRY START         %04Xz %04Xz +%uz\n",
                       p - start, (size_t) try_start->exception_value,
                       (size_t) try_start->exit_value,
                       (size_t) try_start->offset);

            p += sizeof(njs_vmcode_try_start_t);

            continue;
        }

        if (operation == NJS_VMCODE_TRY_BREAK) {
            try_tramp = (njs_vmcode_try_trampoline_t *) p;

            nxt_printf("%05uz TRY BREAK         %04Xz %uz\n",
                       p - start, (size_t) try_tramp->exit_value,
                       (size_t) try_tramp->offset);

            p += sizeof(njs_vmcode_try_trampoline_t);

            continue;
        }

        if (operation == NJS_VMCODE_TRY_CONTINUE) {
            try_tramp = (njs_vmcode_try_trampoline_t *) p;

            nxt_printf("%05uz TRY CONTINUE      %04Xz %uz\n",
                       p - start, (size_t) try_tramp->exit_value,
                       (size_t) try_tramp->offset);

            p += sizeof(njs_vmcode_try_trampoline_t);

            continue;
        }

        if (operation == NJS_VMCODE_TRY_RETURN) {
            try_return = (njs_vmcode_try_return_t *) p;

            nxt_printf("%05uz TRY RETURN        %04Xz %04Xz +%uz\n",
                       p - start, (size_t) try_return->save,
                       (size_t) try_return->retval,
                       (size_t) try_return->offset);

            p += sizeof(njs_vmcode_try_return_t);

            continue;
        }

        if (operation == NJS_VMCODE_CATCH) {
            catch = (njs_vmcode_catch_t *) p;

            nxt_printf("%05uz CATCH             %04Xz +%uz\n",
                       p - start, (size_t) catch->exception,
                       (size_t) catch->offset);

            p += sizeof(njs_vmcode_catch_t);

            continue;
        }

        if (operation == NJS_VMCODE_TRY_END) {
            try_end = (njs_vmcode_try_end_t *) p;

            nxt_printf("%05uz TRY END           +%uz\n",
                       p - start, (size_t) try_end->offset);

            p += sizeof(njs_vmcode_try_end_t);

            continue;
        }

        if (operation == NJS_VMCODE_FINALLY) {
            finally = (njs_vmcode_finally_t *) p;

            nxt_printf("%05uz TRY FINALLY       %04Xz %04Xz +%uz +%uz\n",
                       p - start, (size_t) finally->retval,
                       (size_t) finally->exit_value,
                       (size_t) finally->continue_offset,
                       (size_t) finally->break_offset);

            p += sizeof(njs_vmcode_finally_t);

            continue;
        }

        if (operation == NJS_VMCODE_REFERENCE_ERROR) {
            nxt_printf("%05uz REFERENCE ERROR\n", p - start);

            p += sizeof(njs_vmcode_reference_error_t);

            continue;
        }

        code_name = code_names;
        n = nxt_nitems(code_names);

        do {
            if (operation == code_name->operation) {
                name = &code_name->name;

                if (code_name->size == sizeof(njs_vmcode_3addr_t)) {
                    code3 = (njs_vmcode_3addr_t *) p;

                    nxt_printf("%05uz %*s  %04Xz %04Xz %04Xz\n",
                               p - start, name->length, name->start,
                               (size_t) code3->dst, (size_t) code3->src1,
                               (size_t) code3->src2);

                } else if (code_name->size == sizeof(njs_vmcode_2addr_t)) {
                    code2 = (njs_vmcode_2addr_t *) p;

                    nxt_printf("%05uz %*s  %04Xz %04Xz\n",
                               p - start, name->length, name->start,
                               (size_t) code2->dst, (size_t) code2->src);

                } else if (code_name->size == sizeof(njs_vmcode_1addr_t)) {
                    code1 = (njs_vmcode_1addr_t *) p;

                    nxt_printf("%05uz %*s  %04Xz\n",
                               p - start, name->length, name->start,
                               (size_t) code1->index);
                }

                p += code_name->size;

                goto next;
            }

            code_name++;
            n--;

        } while (n != 0);

        nxt_printf("%05uz UNKNOWN           %04Xz\n",
                   p - start, (size_t) (uintptr_t) operation);

        p += sizeof(njs_vmcode_operation_t);

    next:

        continue;
    }
}
