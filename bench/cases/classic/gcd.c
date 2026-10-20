#include <stdio.h>
int gcd(int a,int b){ while(b){ int t=a%b; a=b; b=t; } return a; }
int main(){
  int i,j,s=0;
  for(i=1;i<=250;i++) for(j=1;j<=250;j++) s+=gcd(i,j);
  printf("%d\n",s);
  return 0;
}
