#include <stdio.h>
int main(){
  long long v; int i; v=1LL;
  for(i=0;i<10;i++) v=v<<1;
  printf("%lld\n",v); v=0x100000000LL; printf("%lld\n",v+1LL); return 0;
}
