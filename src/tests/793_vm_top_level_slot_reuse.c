#include <stdio.h>

int first_values[3];
int second_values[3];

int sum_values(int *p, int n)
{
    int i = 0, sum = 0;
    while (i < n) {
        sum += p[i];
        i++;
    }
    return sum;
}

void set_values(int *p, int n, int value)
{
    int i = 0;
    while (i < n) {
        p[i] = value + i;
        i++;
    }
}

int main(void)
{
    first_values[0] = 1;
    first_values[1] = 2;
    first_values[2] = 3;
    second_values[0] = 10;
    second_values[1] = 20;
    second_values[2] = 30;

    /* Exercise A -> A(different base) -> B -> A -> A(different base) so the
     * top-level FnCodeSlot cache must switch and then reuse side tables safely. */
    printf("%d %d ", sum_values(first_values, 3), sum_values(second_values, 3));
    set_values(first_values, 3, 7);
    printf("%d %d\n", sum_values(first_values, 3), sum_values(second_values, 3));
    return 0;
}
