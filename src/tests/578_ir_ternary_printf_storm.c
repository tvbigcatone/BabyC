#include <stdio.h>
int main() {
    int a=1,b=0,c=1,d=0,e=1;
    printf("%d%d%d%d%d%d%d%d\n",
        a?1:0, b?1:0, c?1:0, d?1:0,
        e?1:0, a==b?1:0, a!=b?1:0, c>d?1:0);
    return 0;
}
