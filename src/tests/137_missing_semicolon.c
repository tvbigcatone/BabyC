/* Bug #6: ParseStatement accetta ';' mancante quando il prossimo token
 * e' un identifier. Questo test verifica che il comportamento sia
 * COERENTE: se il parser accetta "x = 5" senza ';' e poi trova "y = 10",
 * allora il programma gira e stampa quello che ci si aspetta.
 * Se il fix e' applicato, il parser rifiuta e questo test NON parte. */

#include <stdio.h>

void main()
{
    int x;
    int y;
    x = 5
    y = 10;
    printf("%d %d\n", x, y);
}