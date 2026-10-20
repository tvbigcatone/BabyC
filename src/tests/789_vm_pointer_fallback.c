#include <stdio.h>

int first_deref(int *p)
{
    return *p;
}

int first_char_from_table(char **p)
{
    return p[0][0];
}

int walk_pointer(int *p, int n)
{
    int sum = 0;
    while (n > 0) {
        sum += *p;
        p++;
        n--;
    }
    return sum;
}

int main(void)
{
    int values[3] = {7, 11, 13};
    char first[] = "ok";
    char second[] = "no";
    char *names[2] = {first, second};
    printf("%d %d %d\n", first_deref(values), walk_pointer(values, 3),
        first_char_from_table(names));
    return 0;
}
