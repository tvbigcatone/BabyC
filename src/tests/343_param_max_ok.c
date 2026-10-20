/* Anti-regression: 8 params (IR_CALL_MAX_PARAMS) + 16-param def limit is separate */
#include <stdio.h>
int sum8(int a,int b,int c,int d,int e,int f,int g,int h) {
    return a+b+c+d+e+f+g+h;
}
int main() {
    printf("%d\n", sum8(1,1,1,1,1,1,1,1));
    return 0;
}
