#include <stdio.h>

/* Phase A safety check: a struct member of type char (not int) must be
 * loaded/stored through IrbLoadAddr/IrbStoreAddr at 1-byte width -- and
 * wrap the same way (signed char, mod 256) as the token path, exercised
 * by pushing the field past 127 inside the loop. */

struct rec {
    char c;
    int pad;
};

void main()
{
    struct rec r;
    int i;

    r.c = 0;
    r.pad = 0;

    for (i = 0; i < 200; i++)
        r.c = r.c + 1;

    printf("c %d\n", r.c);
}
