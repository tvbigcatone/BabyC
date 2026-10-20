#include <stdio.h>
#define N 400
int v[N];
int gt(int a,int b){ return a>b; }
void swap(int *a,int *b){ int t=*a; *a=*b; *b=t; }
int main(){
  int i,j,x=5,chk=0;
  for(i=0;i<N;i++){ x=(x*75+74)%65537; v[i]=x; }
  for(i=0;i<N-1;i++) for(j=0;j<N-1-i;j++) if(gt(v[j],v[j+1])) swap(&v[j],&v[j+1]);
  for(i=0;i<N;i++) chk=(chk*31+v[i])%1000003;
  printf("%d %d %d\n",v[0],v[N-1],chk);
  return 0;
}
