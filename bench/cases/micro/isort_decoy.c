#include <stdio.h>
/* Perturbed isort: third stmt in while body breaks IrTryFastInsertShift (body_n==2). */
#define N 800
int main() {
    int a[N];
    int i, j, key, pass;
    long long s = 0;
    for (pass = 0; pass < 20; pass++) {
        for (i = 0; i < N; i++) a[i] = (i * 17 + pass * 3) % 997;
        for (i = 1; i < N; i++) {
            key = a[i];
            j = i - 1;
            while (j >= 0 && a[j] > key) {
                a[j + 1] = a[j];
                j--;
                /* decoy: keeps semantics, breaks exact 2-stmt shape */
                key = key;
            }
            a[j + 1] = key;
        }
        s += a[0] + a[N/2] + a[N-1];
    }
    printf("%lld\n", s);
    return 0;
}
