#include <stdio.h>
#define N 500000
char s[N+1];
int main(){
  int i,j,c=0;
  for(i=2;i<=N;i++) s[i]=1;
  for(i=2;i*i<=N;i++) if(s[i]) for(j=i*i;j<=N;j+=i) s[j]=0;
  for(i=2;i<=N;i++) if(s[i]) c++;
  printf("%d\n",c);
  return 0;
}
