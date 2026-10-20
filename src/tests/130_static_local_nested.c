/* static in nested local blocks + persistence across calls */
#include <stdio.h>
void counter(void)
{
    static int n = 0;
    n = n + 1;
    printf("n=%d\n", n);
}
int main()
{
    counter();
    counter();
    {
        static int a = 1;
        printf("a=%d\n", a);
        a = a + 1;
    }
    {
        static int a = 100; /* distinct static in other block */
        printf("a2=%d\n", a);
    }
    counter();
    return 0;
}
