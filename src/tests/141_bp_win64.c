/* Bug #10: BREAKPOINT_HASH_LINE tronca su Win64, e Line e' castato a short.
 * Il test esercita l'uso del debugger in modo indiretto: basta che il
 * programma giri e stampi il risultato corretto. Un bug nel debugger
 * non deve interferire con l'esecuzione normale. */

#include <stdio.h>

int compute(int x)
{
    int y = x * 2;
    y = y + 1;
    return y;
}

void main()
{
    int i;
    int total = 0;
    for (i = 0; i < 100; i++)
        total = total + compute(i);
    /* total = sum(2i+1) per i=0..99 = 2*4950 + 100 = 9900 + 100 = 10000 */
    printf("%d\n", total);
}