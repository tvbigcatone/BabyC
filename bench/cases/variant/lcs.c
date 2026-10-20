#include <stdio.h>
#define L 400
char a[L+1], b[L+1];
int prev[L+1], cur[L+1];
int main(){
  int i,j,x=7;
  for(i=0;i<L;i++){ x=(x*1103+12345)%32768; a[i]='a'+((x>>6)%4); }
  for(i=0;i<L;i++){ x=(x*1103+12345)%32768; b[i]='a'+((x>>6)%4); }
  for(i=1;i<=L;i++){
    cur[0]=0;
    for(j=1;j<=L;j++){
      if(a[i-1]==b[j-1]) cur[j]=prev[j-1]+1;
      else if(prev[j]>cur[j-1]) cur[j]=prev[j];
      else cur[j]=cur[j-1];
    }
    for(j=0;j<=L;j++) prev[j]=cur[j];
  }
  printf("%d\n",prev[L]);
  return 0;
}
