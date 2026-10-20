#include <stdio.h>
int gcd(int a,int b){ return b==0 ? a : gcd(b,a%b); }
int main(){
  int i,j,s=0;
  for(i=1;i<=200;i++) for(j=1;j<=200;j++) s+=gcd(i,j);
  printf("%d\n",s);
  return 0;
}
