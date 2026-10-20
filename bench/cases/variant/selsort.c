#include <stdio.h>
#define N 900
int v[N];
int minidx(int *p,int from,int n){ int m=from,i; for(i=from+1;i<n;i++) if(p[i]<p[m]) m=i; return m; }
int main(){
  int i,m,t,x=11,chk=0;
  for(i=0;i<N;i++){ x=(x*75+74)%65537; v[i]=x; }
  for(i=0;i<N-1;i++){ m=minidx(v,i,N); t=v[i]; v[i]=v[m]; v[m]=t; }
  for(i=0;i<N;i++) chk=(chk*31+v[i])%1000003;
  printf("%d %d %d\n",v[0],v[N-1],chk);
  return 0;
}
