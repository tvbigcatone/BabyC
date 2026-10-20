#include "stdio.h"

void main()
{
    double x = 3.9;
    int y = 4;
    int *z = &y;
    char *msg = "hi there";
    int i;
    printf("%d %d %d %s\n", (int)(x*2), y*2, *z, msg);
    /* sizeof(double) e sizeof(int) sono stabili; per il puntatore
       verifichiamo a runtime che coincida con sizeof(void*),
       così lo stesso .expect vale su 32 e 64 bit. */
    printf("%d %d\n", (int)sizeof(x), (int)sizeof(y));
    if (sizeof(msg) == sizeof(void *))
        printf("ptr_ok\n");
    else
        printf("ptr_bad\n");
    for (i = 1; i <= 3; i++)
        printf("%d\n", i);

    /* this should fail
    { 
        int q = 5;
    }
    q = 3.14; // should say error
    */
}
