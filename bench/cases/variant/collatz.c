#include <stdio.h>
int step(int x){ if(x&1) return 3*x+1; return x>>1; }
int main(){
  int n,x,total=0;
  for(n=2;n<5000;n++){ x=n; while(x!=1){ x=step(x); total++; } }
  printf("%d\n",total);
  return 0;
}
