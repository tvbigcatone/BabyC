/* Regression from samples/004_string_compare.c:
 * char ns[] = {'A','B','C','D'} vs char s[] = "ABCD"
 * Body-IR sizeof(ns) was 1 and sizeof(ns[0]) was 4 → loop count 0. */
#include <stdio.h>

int main(void)
{
    char ns[] = {'A', 'B', 'C', 'D'};
    char s[]  = "ABCD";
    int i;

    printf("\nns ha dimensione %d e quindi contiene %d elementi:\n",
           (int)sizeof(ns), (int)(sizeof(ns)/sizeof(ns[0])));
    for (i = 0; i < (int)(sizeof(ns)/sizeof(ns[0])); ++i)
        printf(" '%c' con codice ASCII %d\n", ns[i], ns[i]);

    printf("\ns ha dimensione %d e quindi contiene %d elementi:\n",
           (int)sizeof(s), (int)(sizeof(s)/sizeof(s[0])));
    for (i = 0; i < (int)(sizeof(s)/sizeof(s[0])); ++i)
        printf(" '%c' con codice ASCII %d\n", s[i], s[i]);

    printf("\n");
    return 0;
}
