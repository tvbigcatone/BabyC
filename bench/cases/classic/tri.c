#include <stdio.h>
int t(int n){ if(n<3) return n; return t(n-1)+t(n-2)+t(n-3)%7; }
int main(){ printf("%d\n",t(22)); return 0; }
