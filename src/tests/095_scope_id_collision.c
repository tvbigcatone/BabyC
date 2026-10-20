#include <stdio.h>
#include <string.h>

int global_counter = 0;

int increment(void)
{
    global_counter++;
    return global_counter;
}

int main()
{
    /* Chiamate ripetute a funzioni di libreria dopo definizione di scope
     * locali: se ScopeID collide, le funzioni di libreria vengono
     * erroneamente marcate OutOfScope */
    printf("%d\n", increment());
    printf("%d\n", increment());

    {
        int local1 = 100;
        printf("%d\n", local1);
        printf("%d\n", increment());
    }

    {
        int local2 = 200;
        printf("%d\n", local2);
        printf("%d\n", strlen("hello"));
    }

    /* Loop con variabile locale in ogni iterazione */
    int i;
    int sum = 0;
    for (i = 0; i < 5; i++) {
        int loop_local = i * 10;
        sum += loop_local;
    }
    printf("%d\n", sum);

    /* Verifica che printf e strlen funzionino ancora */
    printf("still_works\n");
    printf("%d\n", (int)strlen("abc"));

    return 0;
}