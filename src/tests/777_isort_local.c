#include <stdio.h>
#define N 40
int main() {
    int a[N];
    int i, j, key, pass;
    long long s = 0;
    for (pass = 0; pass < 5; pass++) {
        for (i = 0; i < N; i++) a[i] = (i * 17 + pass * 3) % 997;
        for (i = 1; i < N; i++) {
            key = a[i];
            j = i - 1;
            while (j >= 0 && a[j] > key) {
                a[j + 1] = a[j];
                j--;
            }
            a[j + 1] = key;
        }
        s += a[0] + a[N/2] + a[N-1];
    }
    printf("%lld\n", s);
    return 0;
}
