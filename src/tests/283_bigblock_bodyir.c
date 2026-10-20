#include <stdio.h>

/* Big sequential block in add17 exercises function BodyIr (not Irb).
 * Accumulate in long long so the same .expect is valid on LP64 (Unix)
 * and LLP64 (Windows, where long is 32-bit). */

int add17(int a) {
    a = a + 1;
    a = a + 2;
    a = a + 3;
    a = a + 4;
    a = a + 5;
    a = a + 6;
    a = a + 7;
    a = a + 8;
    a = a + 9;
    a = a + 10;
    a = a + 11;
    a = a + 12;
    a = a + 13;
    a = a + 14;
    a = a + 15;
    a = a + 16;
    return a;
}

int main() {
    int i;
    long long sum = 0;
    for (i = 0; i < 2000000; i++) {
        sum = sum + add17(i);
    }
    printf("%lld\n", sum);
    return 0;
}
