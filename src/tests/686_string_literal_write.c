/* Regression: writing through pointer to a string literal must fail at runtime.
 * char *p = "gatto"; p[0] = 'r';  — UB in C; real OS often SIGSEGV.
 * Mutable array char a[] = "gatto"; a[0] = 'r'; remains allowed. */
#include <stdio.h>

int main(void)
{
    char animal_array[] = "cavallo";
    char *animal_punct = "cane";

    printf("array=%s\n", animal_array);
    printf("punct=%s\n", animal_punct);
    animal_punct = "gatto";
    printf("punct=%s\n", animal_punct);

    animal_array[0] = 'C';
    printf("array=%s\n", animal_array);

    animal_punct[0] = 'r';
    printf("should not print\n");
    return 0;
}
