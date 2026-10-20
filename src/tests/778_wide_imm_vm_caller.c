#include <stdio.h>
/* Both mix and main FnCode-eligible — VM→VM must install per-fn WideImm. */
int mix(long long s){
  return (int)(s % 2147483648LL);
}
int main(){
  printf("%d\n", mix(100000000000LL));
  printf("%d\n", mix(2147483648LL));
  printf("%d\n", mix(4294967296LL));
  printf("%d\n", mix(-1LL));
  return 0;
}
