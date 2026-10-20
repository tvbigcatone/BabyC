#include <stdio.h>
#define N 60
int a[N*N], b[N*N], c[N*N];
int main(){
  int i,j,k,chk=0;
  for(i=0;i<N*N;i++){ a[i]=(i*7)%11; b[i]=(i*13+5)%17; }
  for(i=0;i<N;i++)
    for(k=0;k<N;k++)
      for(j=0;j<N;j++)
        c[i*N+j]+=a[i*N+k]*b[k*N+j];
  for(i=0;i<N*N;i++) chk=(chk*31+c[i])%1000003;
  printf("%d\n",chk);
  return 0;
}
