#include <stdio.h>
#define N 70
int a[N][N], b[N][N], c[N][N];
int main(){
  int i,j,k,s,chk=0;
  for(i=0;i<N;i++) for(j=0;j<N;j++){ a[i][j]=(i*7+j*3)%11; b[i][j]=(i*5+j*13)%17; }
  for(i=0;i<N;i++) for(j=0;j<N;j++){
    s=0;
    for(k=0;k<N;k++) s+=a[i][k]*b[k][j];
    c[i][j]=s;
  }
  for(i=0;i<N;i++) for(j=0;j<N;j++) chk=(chk*31+c[i][j])%1000003;
  printf("%d\n",chk);
  return 0;
}
