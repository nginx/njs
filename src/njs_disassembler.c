
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>


typedef struct {
    njs_vmcode_operation_t     operation;
    size_t                     size;
    njs_str_t                  name;
} njs_code_name_t;


static njs_code_name_t  code_names[] = {

    { NJS_VMCODE_OBJECT, sizeof(njs_vmcode_object_t),
          njs_str("OBJECT          ") },
    { NJS_VMCODE_FUNCTION, sizeof(njs_vmcode_function_t),
          njs_str("FUNCTION        ") },
    { NJS_VMCODE_THIS, sizeof(njs_vmcode_this_t),
          njs_str("THIS            ") },
    { NJS_VMCODE_ARGUMENTS, sizeof(njs_vmcode_arguments_t),
          njs_str("ARGUMENTS       ") },
    { NJS_VMCODE_REGEXP, sizeof(njs_vmcode_regexp_t),
          njs_str("REGEXP          ") },
    { NJS_VMCODE_TEMPLATE_LITERAL, sizeof(njs_vmcode_template_literal_t),
          njs_str("TEMPLATE LITERAL") },
    { NJS_VMCODE_OBJECT_COPY, sizeof(njs_vmcode_object_copy_t),
          njs_str("OBJECT COPY     ") },

    { NJS_VMCODE_FUNCTION_COPY, sizeof(njs_vmcode_function_copy_t),
          njs_str("FUNCTION COPY   ") },

    { NJS_VMCODE_PROPERTY_GET, sizeof(njs_vmcode_prop_get_t),
          njs_str("PROP GET        ") },
    { NJS_VMCODE_GLOBAL_GET, sizeof(njs_vmcode_prop_get_t),
          njs_str("GLOBAL GET      ") },
    { NJS_VMCODE_PROPERTY_INIT, sizeof(njs_vmcode_prop_set_t),
          njs_str("PROP INIT       ") },
    { NJS_VMCODE_PROTO_INIT, sizeof(njs_vmcode_prop_set_t),
          njs_str("PROTO INIT      ") },
    { NJS_VMCODE_PROPERTY_SET, sizeof(njs_vmcode_prop_set_t),
          njs_str("PROP SET        ") },
    { NJS_VMCODE_PROPERTY_IN, sizeof(njs_vmcode_3addr_t),
          njs_str("PROP IN         ") },
    { NJS_VMCODE_PROPERTY_DELETE, sizeof(njs_vmcode_3addr_t),
          njs_str("PROP DELETE     ") },
    { NJS_VMCODE_INSTANCE_OF, sizeof(njs_vmcode_instance_of_t),
          njs_str("INSTANCE OF     ") },

    { NJS_VMCODE_FUNCTION_CALL, sizeof(njs_vmcode_function_call_t),
          njs_str("FUNCTION CALL   ") },
    { NJS_VMCODE_RETURN, sizeof(njs_vmcode_return_t),
          njs_str("RETURN          ") },
    { NJS_VMCODE_STOP, sizeof(njs_vmcode_stop_t),
          njs_str("STOP            ") },

    { NJS_VMCODE_INCREMENT, sizeof(njs_vmcode_3addr_t),
          njs_str("INC             ") },
    { NJS_VMCODE_DECREMENT, sizeof(njs_vmcode_3addr_t),
          njs_str("DEC             ") },
    { NJS_VMCODE_POST_INCREMENT, sizeof(njs_vmcode_3addr_t),
          njs_str("POST INC        ") },
    { NJS_VMCODE_POST_DECREMENT, sizeof(njs_vmcode_3addr_t),
          njs_str("POST DEC        ") },

    { NJS_VMCODE_DELETE, sizeof(njs_vmcode_2addr_t),
          njs_str("DELETE          ") },
    { NJS_VMCODE_VOID, sizeof(njs_vmcode_2addr_t),
          njs_str("VOID            ") },
    { NJS_VMCODE_TYPEOF, sizeof(njs_vmcode_2addr_t),
          njs_str("TYPEOF          ") },

    { NJS_VMCODE_UNARY_PLUS, sizeof(njs_vmcode_2addr_t),
          njs_str("PLUS            ") },
    { NJS_VMCODE_UNARY_NEGATION, sizeof(njs_vmcode_2addr_t),
          njs_str("NEGATION        ") },

    { NJS_VMCODE_ADDITION, sizeof(njs_vmcode_3addr_t),
          njs_str("ADD             ") },
    { NJS_VMCODE_SUBSTRACTION, sizeof(njs_vmcode_3addr_t),
          njs_str("SUBSTRACT       ") },
    { NJS_VMCODE_MULTIPLICATION, sizeof(njs_vmcode_3addr_t),
          njs_str("MULTIPLY        ") },
    { NJS_VMCODE_EXPONENTIATION, sizeof(njs_vmcode_3addr_t),
          njs_str("POWER           ") },
    { NJS_VMCODE_DIVISION, sizeof(njs_vmcode_3addr_t),
          njs_str("DIVIDE          ") },
    { NJS_VMCODE_REMAINDER, sizeof(njs_vmcode_3addr_t),
          njs_str("REMAINDER       ") },

    { NJS_VMCODE_LEFT_SHIFT, sizeof(njs_vmcode_3addr_t),
          njs_str("LEFT SHIFT      ") },
    { NJS_VMCODE_RIGHT_SHIFT, sizeof(njs_vmcode_3addr_t),
          njs_str("RIGHT SHIFT     ") },
    { NJS_VMCODE_UNSIGNED_RIGHT_SHIFT, sizeof(njs_vmcode_3addr_t),
          njs_str("USGN RIGHT SHIFT") },

    { NJS_VMCODE_LOGICAL_NOT, sizeof(njs_vmcode_2addr_t),
          njs_str("LOGICAL NOT     ") },

    { NJS_VMCODE_BITWISE_NOT, sizeof(njs_vmcode_2addr_t),
          njs_str("BINARY NOT      ") },
    { NJS_VMCODE_BITWISE_AND, sizeof(njs_vmcode_3addr_t),
          njs_str("BINARY AND      ") },
    { NJS_VMCODE_BITWISE_XOR, sizeof(njs_vmcode_3addr_t),
          njs_str("BINARY XOR      ") },
    { NJS_VMCODE_BITWISE_OR, sizeof(njs_vmcode_3addr_t),
          njs_str("BINARY OR       ") },

    { NJS_VMCODE_EQUAL, sizeof(njs_vmcode_3addr_t),
          njs_str("EQUAL           ") },
    { NJS_VMCODE_NOT_EQUAL, sizeof(njs_vmcode_3addr_t),
          njs_str("NOT EQUAL       ") },
    { NJS_VMCODE_LESS, sizeof(njs_vmcode_3addr_t),
          njs_str("LESS            ") },
    { NJS_VMCODE_LESS_OR_EQUAL, sizeof(njs_vmcode_3addr_t),
          njs_str("LESS OR EQUAL   ") },
    { NJS_VMCODE_GREATER, sizeof(njs_vmcode_3addr_t),
          njs_str("GREATER         ") },
    { NJS_VMCODE_GREATER_OR_EQUAL, sizeof(njs_vmcode_3addr_t),
          njs_str("GREATER OR EQUAL") },

    { NJS_VMCODE_STRICT_EQUAL, sizeof(njs_vmcode_3addr_t),
          njs_str("STRICT EQUAL    ") },
    { NJS_VMCODE_STRICT_NOT_EQUAL, sizeof(njs_vmcode_3addr_t),
          njs_str("STRICT NOT EQUAL") },

    { NJS_VMCODE_MOVE, sizeof(njs_vmcode_move_t),
          njs_str("MOVE            ") },

    { NJS_VMCODE_THROW, sizeof(njs_vmcode_throw_t),
          njs_str("THROW           ") },

    { NJS_VMCODE_LET, sizeof(njs_vmcode_variable_t),
          njs_str("LET             ") },

    { NJS_VMCODE_LET_UPDATE, sizeof(njs_vmcode_variable_t),
          njs_str("LET UPDATE      ") },

    { NJS_VMCODE_INITIALIZATION_TEST, sizeof(njs_vmcode_variable_t),
          njs_str("INIT TEST       ") },

    { NJS_VMCODE_NOT_INITIALIZED, sizeof(njs_vmcode_variable_t),
          njs_str("NOT INIT        ") },

    { NJS_VMCODE_ASSIGNMENT_ERROR, sizeof(njs_vmcode_variable_t),
          njs_str("ASSIGNMENT ERROR") },

    { NJS_VMCODE_DEBUGGER, sizeof(njs_vmcode_debugger_t),
          njs_str("DEBUGGER        ") },

    { NJS_VMCODE_AWAIT, sizeof(njs_vmcode_await_t),
          njs_str("AWAIT           ") },
};


void
njs_disassembler(njs_vm_t *vm)
{
    njs_uint_t     n;
    njs_vm_code_t  *code;

    code = vm->codes->start;
    n = vm->codes->items;

    while (n != 0) {
        njs_printf("%V:%V\n", &code->file, &code->name);
        njs_disassemble(code->start, code->end, -1, code->lines);
        code++;
        n--;
    }

    njs_printf("\n");
}


void
njs_disassemble(u_char *start, u_char *end, njs_int_t count, njs_arr_t *lines)
{
    u_char                       *p;
    uint32_t                     line;
    njs_str_t                    *name;
    njs_uint_t                   n;
    const char                   *type;
    njs_code_name_t              *code_name;
    njs_vmcode_jump_t            *jump;
    njs_vmcode_error_t           *error;
    njs_vmcode_1addr_t           *code1;
    njs_vmcode_2addr_t           *code2;
    njs_vmcode_3addr_t           *code3;
    njs_vmcode_array_t           *array;
    njs_vmcode_catch_t           *catch;
    njs_vmcode_import_t          *import;
    njs_vmcode_finally_t         *finally;
    njs_vmcode_try_end_t         *try_end;
    njs_vmcode_move_arg_t        *move_arg;
    njs_vmcode_try_start_t       *try_start;
    njs_vmcode_operation_t       operation;
    njs_vmcode_cond_jump_t       *cond_jump;
    njs_vmcode_test_jump_t       *test_jump;
    njs_vmcode_prop_next_t       *prop_next;
    njs_vmcode_try_return_t      *try_return;
    njs_vmcode_equal_jump_t      *equal;
    njs_vmcode_prop_foreach_t    *prop_foreach;
    njs_vmcode_method_frame_t    *method;
    njs_vmcode_prop_accessor_t   *prop_accessor;
    njs_vmcode_try_trampoline_t  *try_tramp;
    njs_vmcode_function_frame_t  *function;

    /*
     * On some 32-bit platform uintptr_t is int and compilers warn
     * about %l format modifier.  size_t has the size as pointer so
     * there is no run-time overhead.
     */

    p = start;

    while (((p < end) && (count == -1)) || (count-- > 0)) {
        operation = *(njs_vmcode_operation_t *) p;
        line = njs_lookup_line(lines, p - start);

        if (operation == NJS_VMCODE_ARRAY) {
            array = (njs_vmcode_array_t *) p;

            njs_printf("%5uD | %05uz ARRAY             %04Xz %uz%s\n",
                       line, p - start, (size_t) array->retval,
                       (size_t) array->length, array->ctor ? " INIT" : "");

            p += sizeof(njs_vmcode_array_t);

            continue;
        }

        if (operation == NJS_VMCODE_IF_TRUE_JUMP) {
            cond_jump = (njs_vmcode_cond_jump_t *) p;

            njs_printf("%5uD | %05uz JUMP IF TRUE      %04Xz %z\n",
                       line, p - start, (size_t) cond_jump->cond,
                       (size_t) cond_jump->offset);

            p += sizeof(njs_vmcode_cond_jump_t);

            continue;
        }

        if (operation == NJS_VMCODE_IF_FALSE_JUMP) {
            cond_jump = (njs_vmcode_cond_jump_t *) p;

            njs_printf("%5uD | %05uz JUMP IF FALSE     %04Xz %z\n",
                       line, p - start, (size_t) cond_jump->cond,
                       (size_t) cond_jump->offset);

            p += sizeof(njs_vmcode_cond_jump_t);

            continue;
        }

        if (operation == NJS_VMCODE_JUMP) {
            jump = (njs_vmcode_jump_t *) p;

            njs_printf("%5uD | %05uz JUMP              %z\n",
                       line, p - start, (size_t) jump->offset);

            p += sizeof(njs_vmcode_jump_t);

            continue;
        }

        if (operation == NJS_VMCODE_IF_EQUAL_JUMP) {
            equal = (njs_vmcode_equal_jump_t *) p;

            njs_printf("%5uD | %05uz JUMP IF EQUAL     %04Xz %04Xz %z\n",
                       line, p - start, (size_t) equal->value1,
                       (size_t) equal->value2, (size_t) equal->offset);

            p += sizeof(njs_vmcode_equal_jump_t);

            continue;
        }

        if (operation == NJS_VMCODE_TEST_IF_TRUE) {
            test_jump = (njs_vmcode_test_jump_t *) p;

            njs_printf("%5uD | %05uz TEST IF TRUE      %04Xz %04Xz %z\n",
                       line, p - start, (size_t) test_jump->retval,
                       (size_t) test_jump->value, (size_t) test_jump->offset);

            p += sizeof(njs_vmcode_test_jump_t);

            continue;
        }

        if (operation == NJS_VMCODE_TEST_IF_FALSE) {
            test_jump = (njs_vmcode_test_jump_t *) p;

            njs_printf("%5uD | %05uz TEST IF FALSE     %04Xz %04Xz %z\n",
                       line, p - start, (size_t) test_jump->retval,
                       (size_t) test_jump->value, (size_t) test_jump->offset);

            p += sizeof(njs_vmcode_test_jump_t);

            continue;
        }

        if (operation == NJS_VMCODE_COALESCE) {
            test_jump = (njs_vmcode_test_jump_t *) p;

            njs_printf("%5uD | %05uz COALESCE          %04Xz %04Xz %z\n",
                       line, p - start, (size_t) test_jump->retval,
                       (size_t) test_jump->value, (size_t) test_jump->offset);

            p += sizeof(njs_vmcode_test_jump_t);

            continue;
        }

        if (operation == NJS_VMCODE_FUNCTION_FRAME) {
            function = (njs_vmcode_function_frame_t *) p;

            njs_printf("%5uD | %05uz FUNCTION FRAME    %04Xz %uz%s\n",
                       line, p - start, (size_t) function->name,
                       function->nargs, function->ctor ? " CTOR" : "");

            p += sizeof(njs_vmcode_function_frame_t);

            continue;
        }

        if (operation == NJS_VMCODE_METHOD_FRAME) {
            method = (njs_vmcode_method_frame_t *) p;

            njs_printf("%5uD | %05uz METHOD FRAME      %04Xz %04Xz %uz%s\n",
                       line, p - start, (size_t) method->object,
                       (size_t) method->method, method->nargs,
                       method->ctor ? " CTOR" : "");

            p += sizeof(njs_vmcode_method_frame_t);
            continue;
        }

        if (operation == NJS_VMCODE_PROPERTY_FOREACH) {
            prop_foreach = (njs_vmcode_prop_foreach_t *) p;

            njs_printf("%5uD | %05uz PROP FOREACH      %04Xz %04Xz %z\n",
                       line, p - start, (size_t) prop_foreach->next,
                       (size_t) prop_foreach->object,
                       (size_t) prop_foreach->offset);

            p += sizeof(njs_vmcode_prop_foreach_t);
            continue;
        }

        if (operation == NJS_VMCODE_PROPERTY_NEXT) {
            prop_next = (njs_vmcode_prop_next_t *) p;

            njs_printf("%5uD | %05uz PROP NEXT         %04Xz %04Xz %04Xz %z\n",
                       line, p - start, (size_t) prop_next->retval,
                       (size_t) prop_next->object, (size_t) prop_next->next,
                       (size_t) prop_next->offset);

            p += sizeof(njs_vmcode_prop_next_t);

            continue;
        }

        if (operation == NJS_VMCODE_PROPERTY_ACCESSOR) {
            prop_accessor = (njs_vmcode_prop_accessor_t *) p;

            njs_printf("%5uD | %05uz PROP %s ACCESSOR %04Xz %04Xz %04Xz\n",
                       line, p - start,
                       (prop_accessor->type == NJS_OBJECT_PROP_GETTER)
                           ? "GET" : "SET",
                       (size_t) prop_accessor->value,
                       (size_t) prop_accessor->object,
                       (size_t) prop_accessor->property);

            p += sizeof(njs_vmcode_prop_accessor_t);

            continue;
        }

        if (operation == NJS_VMCODE_IMPORT) {
            import = (njs_vmcode_import_t *) p;

            njs_printf("%5uD | %05uz IMPORT            %04Xz %V\n",
                       line, p - start, (size_t) import->retval,
                       &import->module->name);

            p += sizeof(njs_vmcode_import_t);

            continue;
        }

        if (operation == NJS_VMCODE_TRY_START) {
            try_start = (njs_vmcode_try_start_t *) p;

            njs_printf("%5uD | %05uz TRY START         %04Xz %04Xz %z\n",
                       line, p - start, (size_t) try_start->exception_value,
                       (size_t) try_start->exit_value,
                       (size_t) try_start->offset);

            p += sizeof(njs_vmcode_try_start_t);

            continue;
        }

        if (operation == NJS_VMCODE_TRY_BREAK) {
            try_tramp = (njs_vmcode_try_trampoline_t *) p;

            njs_printf("%5uD | %05uz TRY BREAK         %04Xz %z\n",
                       line, p - start, (size_t) try_tramp->exit_value,
                       (size_t) try_tramp->offset);

            p += sizeof(njs_vmcode_try_trampoline_t);

            continue;
        }

        if (operation == NJS_VMCODE_TRY_CONTINUE) {
            try_tramp = (njs_vmcode_try_trampoline_t *) p;

            njs_printf("%5uD | %05uz TRY CONTINUE      %04Xz %z\n",
                       line, p - start, (size_t) try_tramp->exit_value,
                       (size_t) try_tramp->offset);

            p += sizeof(njs_vmcode_try_trampoline_t);

            continue;
        }

        if (operation == NJS_VMCODE_TRY_RETURN) {
            try_return = (njs_vmcode_try_return_t *) p;

            njs_printf("%5uD | %05uz TRY RETURN        %04Xz %04Xz %z\n",
                       line, p - start, (size_t) try_return->save,
                       (size_t) try_return->retval,
                       (size_t) try_return->offset);

            p += sizeof(njs_vmcode_try_return_t);

            continue;
        }

        if (operation == NJS_VMCODE_CATCH) {
            catch = (njs_vmcode_catch_t *) p;

            njs_printf("%5uD | %05uz CATCH             %04Xz %z\n",
                       line, p - start, (size_t) catch->exception,
                       (size_t) catch->offset);

            p += sizeof(njs_vmcode_catch_t);

            continue;
        }

        if (operation == NJS_VMCODE_TRY_END) {
            try_end = (njs_vmcode_try_end_t *) p;

            njs_printf("%5uD | %05uz TRY END           %z\n",
                       line, p - start, (size_t) try_end->offset);

            p += sizeof(njs_vmcode_try_end_t);

            continue;
        }

        if (operation == NJS_VMCODE_FINALLY) {
            finally = (njs_vmcode_finally_t *) p;

            njs_printf("%5uD | %05uz TRY FINALLY       %04Xz %04Xz %z %z\n",
                       line, p - start, (size_t) finally->retval,
                       (size_t) finally->exit_value,
                       (size_t) finally->continue_offset,
                       (size_t) finally->break_offset);

            p += sizeof(njs_vmcode_finally_t);

            continue;
        }

        if (operation == NJS_VMCODE_ERROR) {
            error = (njs_vmcode_error_t *) p;

            switch (error->type) {
            case NJS_OBJ_TYPE_REF_ERROR:
                type = "REFERENCE";
                break;

            case NJS_OBJ_TYPE_TYPE_ERROR:
                type = "TYPE";
                break;

            case NJS_OBJ_TYPE_ERROR:
            default:
                type = "";
            }

            njs_printf("%5uD | %05uz %s ERROR\n", line, p - start, type);

            p += sizeof(njs_vmcode_error_t);

            continue;
        }

        if (operation == NJS_VMCODE_MOVE_ARG) {
            move_arg = (njs_vmcode_move_arg_t *) p;

            njs_printf("%5uD | %05uz MOVE ARGUMENT     %uD %04Xz\n",
                       line, p - start, move_arg->dst, (size_t) move_arg->src);

            p += sizeof(njs_vmcode_move_arg_t);

            continue;
        }

        code_name = code_names;
        n = njs_nitems(code_names);

        do {
            if (operation == code_name->operation) {
                name = &code_name->name;

                if (code_name->size == sizeof(njs_vmcode_3addr_t)) {
                    code3 = (njs_vmcode_3addr_t *) p;

                    njs_printf("%5uD | %05uz %*s  %04Xz %04Xz %04Xz\n",
                               line, p - start, name->length, name->start,
                               (size_t) code3->dst, (size_t) code3->src1,
                               (size_t) code3->src2);

                } else if (code_name->size == sizeof(njs_vmcode_2addr_t)) {
                    code2 = (njs_vmcode_2addr_t *) p;

                    njs_printf("%5uD | %05uz %*s  %04Xz %04Xz\n",
                               line, p - start, name->length, name->start,
                               (size_t) code2->dst, (size_t) code2->src);

                } else if (code_name->size == sizeof(njs_vmcode_1addr_t)) {
                    code1 = (njs_vmcode_1addr_t *) p;

                    njs_printf("%5uD | %05uz %*s  %04Xz\n",
                               line, p - start, name->length, name->start,
                               (size_t) code1->index);
                }

                p += code_name->size;

                goto next;
            }

            code_name++;
            n--;

        } while (n != 0);

        njs_printf("%5uD | %05uz UNKNOWN           %04Xz\n", line,
                   p - start, (size_t) (uintptr_t) operation);

        p += sizeof(njs_vmcode_operation_t);

    next:

        continue;
    }
}
