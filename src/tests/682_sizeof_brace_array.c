/* Regression: sizeof on unsized array with brace init + sizeof(a[i]).
 * Body-IR left IrCompileLocalElemSize at elem-size (1) for char a[]={..},
 * so sizeof(a)==1; sizeof(a[0]) fell through to sizeof(int)==4. */
#include <stdio.h>

int main()
{
    char ns[] = {'A', 'B', 'C', 'D'};
    char s[]  = "ABCD";
    char t[4] = {'A', 'B', 'C', 'D'};
    int  iv[] = {10, 20, 30};

    printf("ns=%d ns0=%d div=%d\n",
           (int)sizeof(ns), (int)sizeof(ns[0]),
           (int)(sizeof(ns)/sizeof(ns[0])));
    printf("s=%d s0=%d\n", (int)sizeof(s), (int)sizeof(s[0]));
    printf("t=%d\n", (int)sizeof(t));
    printf("iv=%d iv0=%d n=%d\n",
           (int)sizeof(iv), (int)sizeof(iv[0]),
           (int)(sizeof(iv)/sizeof(iv[0])));

    {
        int i;
        for (i = 0; i < (int)(sizeof(ns)/sizeof(ns[0])); i++)
            printf("%c", ns[i]);
        printf("\n");
    }
    return 0;
}
