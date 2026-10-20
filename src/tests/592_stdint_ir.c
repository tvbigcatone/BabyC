#include <stdio.h>
#include <stdint.h>
int main() {
    int64_t x = -1;
    uint64_t y = 1ULL << 40;
    int32_t z = 123456789;
    printf("%lld\n", (long long)x);
    printf("%llu\n", (unsigned long long)y);
    printf("%d\n", z);
    return 0;
}
