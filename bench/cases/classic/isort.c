#include <stdio.h>
#define N 1200
int v[N];
int main(){
  int i,j,t,x=12345,chk=0;
  for(i=0;i<N;i++){ x=(x*75)%65537; v[i]=x; }
  for(i=1;i<N;i++){
    t=v[i]; j=i-1;
    while(j>=0 && v[j]>t){ v[j+1]=v[j]; j--; }
    v[j+1]=t;
  }
  for(i=0;i<N;i++) chk=(chk*31+v[i])%1000003;
  printf("%d %d %d\n",v[0],v[N-1],chk);
  return 0;
}
