#include <openssl/ecdsa.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

int main(int argc, char * argv[]) {
    int                  rbytes, sbytes, len, n;
    ECDSA_SIG            *ecSig;
    unsigned char        *p, *end;
    const unsigned char  *start;
    unsigned char        der[512];
    unsigned char        out[64];

    p = der;
    end = &der[sizeof(der)];

    for ( ;; ) {
        n = read(STDIN_FILENO, der, end - p);

        if (n == 0) {
            break;
        }

        if ((end - p) == 0) {
            printf("too large (> 512) der length in stdin");
            return EXIT_FAILURE;
        }

        p += n;
    }

    start = der;
    ecSig = d2i_ECDSA_SIG(NULL, &start, p - der);
    if (ecSig == NULL) {
        printf("d2i_ECDSA_SIG() failed");
        return EXIT_FAILURE;
    }

    rbytes = BN_num_bytes(ECDSA_SIG_get0_r(ecSig));
    sbytes = BN_num_bytes(ECDSA_SIG_get0_s(ecSig));

    BN_bn2binpad(ECDSA_SIG_get0_r(ecSig), out, rbytes);
    BN_bn2binpad(ECDSA_SIG_get0_s(ecSig), &out[32], sbytes);

    write(STDOUT_FILENO, out, sizeof(out));

    return EXIT_SUCCESS;
}
