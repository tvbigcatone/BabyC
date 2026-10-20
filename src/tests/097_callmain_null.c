#include <stdio.h>

/* main con parametri: esercita il path PicocCallMain con NumParams != 0 */
int main(int argc, char **argv)
{
    printf("argc=%d\n", argc);
    if (argc >= 1) {
        printf("argv0_set=%d\n", argv[0] != 0);
    }
    printf("callmain_ok\n");
    return 0;
}