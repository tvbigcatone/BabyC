/* while + return inside function body (kernel while + thin when no ptr params) */
#include <stdio.h>
static int find(int n, int target) {
    int i;
    i = 0;
    while (i < n) {
        if (i == target) return i;
        i++;
    }
    return -1;
}
int main(void) {
    printf("%d %d\n", find(10, 7), find(10, 99));
    return 0;
}
