#include <stdio.h>

void bubble(int *a, int n) {
    int i, j, t;
    for (i = 0; i < n - 1; i = i + 1)
        for (j = 0; j < n - 1 - i; j = j + 1)
            if (a[j] > a[j + 1]) {
                t = a[j];
                a[j] = a[j + 1];
                a[j + 1] = t;
            }
}

int is_sorted(int *a, int n) {
    int i;
    for (i = 0; i < n - 1; i = i + 1)
        if (a[i] > a[i + 1])
            return 0;
    return 1;
}

int main() {
    int arr[8];
    int i;
    arr[0] = 5; arr[1] = 2; arr[2] = 8; arr[3] = 1;
    arr[4] = 9; arr[5] = 3; arr[6] = 7; arr[7] = 4;
    bubble(arr, 8);
    for (i = 0; i < 8; i = i + 1) {
        if (i) printf(" ");
        printf("%d", arr[i]);
    }
    printf("\n%d\n", is_sorted(arr, 8));
    return 0;
}
