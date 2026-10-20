#include <stdio.h>

int main()
{
    /* Array 2D inizializzato con lista piatta: il parser deve calcolare
     * correttamente l'offset di ogni elemento */
    int m[3][3] = {1,2,3,4,5,6,7,8,9};

    printf("%d\n", m[0][0]);
    printf("%d\n", m[0][1]);
    printf("%d\n", m[0][2]);
    printf("%d\n", m[1][0]);
    printf("%d\n", m[1][1]);
    printf("%d\n", m[1][2]);
    printf("%d\n", m[2][0]);
    printf("%d\n", m[2][1]);
    printf("%d\n", m[2][2]);

    /* Array 2D di char inizializzato con stringhe: caso speciale
     * menzionato nel codice (char x[10][10] = {"abc", "def"}) */
    char names[3][8] = {"abc", "de", "f"};
    printf("%s\n", names[0]);
    printf("%s\n", names[1]);
    printf("%s\n", names[2]);

    /* Verifica offset via pointer arithmetic */
    int *p = &m[0][0];
    int sum = 0;
    int i;
    for (i = 0; i < 9; i++)
        sum += p[i];
    printf("%d\n", sum);

    return 0;
}