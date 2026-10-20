#include <stdio.h>
int f(long long x){ return (int)(x % 2147483648LL); }
int g(long long x){ return (int)(x / 2147483648LL); }
int h(long long x){ return (int)(x % 2147483647LL); }
int main(){
  long long v = 100000000000LL;
  printf("%d\n", f(v));
  printf("%d\n", g(v));
  printf("%d\n", h(v));
  printf("%d\n", f(2147483648LL));
  printf("%d\n", f(4294967296LL));
  return 0;
}
