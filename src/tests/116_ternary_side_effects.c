/* Verifica che il ternario esegua SOLO il ramo preso e che gli operatori
 * dopo ':' vengano eseguiti correttamente.
 * Bug #10: IgnorePrecedence non resettato dopo ':' puo' saltare operatori
 * successivi al ternario. */

#include <stdio.h>

int side_effect = 0;

int inc()
{
    side_effect++;
    return side_effect;
}

void main()
{
    int a = 0;
    int b = 0;
    int c = 0;
    int r;

    /* Solo il then-branch deve avere effetti collaterali */
    r = (1 ? inc() : 999);
    printf("%d %d\n", r, side_effect);      /* atteso: 1 1 */

    /* Solo l'else-branch deve avere effetti collaterali */
    r = (0 ? 999 : inc());
    printf("%d %d\n", r, side_effect);      /* atteso: 2 2 */

    /* Operatore dopo ':' DEVE essere eseguito */
    r = 1 ? 10 : 20 + 5;
    printf("%d\n", r);                      /* atteso: 10 */

    r = 0 ? 10 : 20 + 5;
    printf("%d\n", r);                      /* atteso: 25 */

    /* Ternario annidato a destra */
    r = 1 ? (0 ? 100 : 200) : 300;
    printf("%d\n", r);                      /* atteso: 200 */

    /* Ternario con effetti in entrambi i rami, uno solo deve scattare */
    a = 0; b = 0; c = 0;
    r = (a ? b++ : c++);
    printf("%d %d %d\n", r, b, c);          /* atteso: 0 0 1 */

    a = 1; b = 0; c = 0;
    r = (a ? b++ : c++);
    printf("%d %d %d\n", r, b, c);          /* atteso: 0 1 0 */
}