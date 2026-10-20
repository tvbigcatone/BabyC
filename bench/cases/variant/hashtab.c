#include <stdio.h>
#define M 4096
int keys[M], used[M];
int main(){
  int i,x=1,k,h,hits=0,ins=0;
  for(i=0;i<2500;i++){
    x=(x*1103+12345)%65536; k=x; h=(k*31)%M;
    while(used[h] && keys[h]!=k) h=(h+1)%M;
    if(!used[h]){ used[h]=1; keys[h]=k; ins++; }
  }
  x=1;
  for(i=0;i<20000;i++){
    x=(x*1103+12345)%65536; k=x; h=(k*31)%M;
    while(used[h] && keys[h]!=k) h=(h+1)%M;
    if(used[h]) hits++;
  }
  printf("%d %d\n",ins,hits);
  return 0;
}
