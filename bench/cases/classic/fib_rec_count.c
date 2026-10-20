#include <stdio.h>
int calls=0;
int fib(int n){ calls++; if(n<2) return n; return fib(n-1)+fib(n-2); }
int main(){ printf("%d\n",fib(25)); printf("%d\n",calls); return 0; }
