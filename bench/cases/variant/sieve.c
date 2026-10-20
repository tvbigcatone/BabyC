#include <stdio.h>
#define N 400001
int flag[N+1];
int count(int n){ int i,c=0; for(i=2;i<=n;i++) if(flag[i]==0) c++; return c; }
int main(){
  int i,j;
  i=2;
  while(i<=N){
    if(flag[i]==0){ j=i+i; while(j<=N){ flag[j]=1; j+=i; } }
    i++;
  }
  printf("%d\n",count(N));
  return 0;
}
