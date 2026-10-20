#include <stdio.h>
#define N 9
int cols[N], d1[2*N], d2[2*N];
int solve(int r){
  int c, cnt=0;
  if(r==N) return 1;
  for(c=0;c<N;c++){
    if(cols[c] || d1[r+c] || d2[r-c+N]) continue;
    cols[c]=1; d1[r+c]=1; d2[r-c+N]=1;
    cnt+=solve(r+1);
    cols[c]=0; d1[r+c]=0; d2[r-c+N]=0;
  }
  return cnt;
}
int main(){ printf("%d\n",solve(0)); return 0; }
