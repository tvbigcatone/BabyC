#include <stdio.h>
int array[8] = {5, 3, 8, 1, 9, 2, 7, 4};
void swap(int a, int b) { int t = array[a]; array[a] = array[b]; array[b] = t; }
int partition(int left, int right)
{
    int pv = array[left]; int index = left; int i;
    for (i = left; i < right; i++)
        if (array[i] < pv) { swap(i, index); index += 1; }
    return index;
}
int wrap(int l, int r) { int x = partition(l, r); return x; }
int main(void)
{
    int a = wrap(0, 7); int b = wrap(0, 3); int c = wrap(5, 7);
    printf("%d %d %d\n", a, b, c);
    return 0;
}
