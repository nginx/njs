#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* Mock njs_vsprintf to test buffer bounds */
extern char *njs_vsprintf(char *buf, char *last, const char *fmt, va_list args);

START_TEST(test_buffer_bounds_vsprintf)
{
    /* Invariant: njs_vsprintf must never write beyond allocated buffer bounds */
    
    /* Test payloads: oversized inputs that could overflow 32-byte buffers */
    const char *payloads[] = {
        "normal_host",                                    /* valid input */
        "a.b.c.d.e.f.g.h.i.j.k.l.m.n.o.p.q.r.s.t.u.v", /* 44 bytes - exceeds 32 */
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", /* 64 bytes */
        "127.0.0.1:99999",                                /* boundary: large port */
        "x" /* 1 byte - minimum valid */
    };
    int num_payloads = sizeof(payloads) / sizeof(payloads[0]);
    
    for (int i = 0; i < num_payloads; i++) {
        char buffer[32];
        char *end = buffer + sizeof(buffer);
        char *result;
        
        /* Fill buffer with sentinel to detect overflow */
        memset(buffer, 0xAA, sizeof(buffer));
        
        /* Call njs_vsprintf with format string and payload */
        va_list args;
        va_start(args, payloads[i]);
        result = njs_vsprintf(buffer, end, "host:%s", args);
        va_end(args);
        
        /* Verify result pointer is within bounds */
        ck_assert_ptr_ge(result, buffer);
        ck_assert_ptr_le(result, end);
        
        /* Verify no write beyond buffer end (check sentinel bytes) */
        unsigned char *check_ptr = (unsigned char *)end;
        ck_assert_int_eq(check_ptr[0], 0xAA);
    }
}
END_TEST

Suite *security_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("Security");
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_buffer_bounds_vsprintf);
    suite_add_tcase(s, tc_core);

    return s;
}

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = security_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}