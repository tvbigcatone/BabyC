/* Regression: array-of-struct member char[] as printf %s arg.
 * Body-IR Intrinsic packing used to only detect array members on bare
 * Ident bases (s.a), not Index (arr[i].a) → packed as int → "XXX". */
#include <stdio.h>
#include <string.h>

struct Cane {
    char indice[4];
    char nome[40];
    char razza[40];
    char peso[40];
} item[3];

struct Solo {
    int n;
    char tag[8];
} one;

int main()
{
    char c1[4] = "N.1";
    char c2[30] = "nome1";
    char c3[30] = "razza1";
    char c4[30] = "peso";

    strcpy(item[0].indice, c1);
    strcpy(item[0].nome, c2);
    strcpy(item[0].razza, c3);
    strcpy(item[0].peso, c4);

    printf("campo1 = %s\n", item[0].indice);
    printf("campo2 = %s\n", item[0].nome);
    printf("campo3 = %s\n", item[0].razza);
    printf("campo4 = %s\n", item[0].peso);

    one.n = 7;
    strcpy(one.tag, "solo");
    printf("%d %s\n", one.n, one.tag);

    return 0;
}
