#include <stdio.h>
#define N 500
struct P { int x; int y; int v; };
struct P ps[N];
int main(){
  int i,r,chk=0;
  for(i=0;i<N;i++){ ps[i].x=i; ps[i].y=N-i; ps[i].v=1; }
  for(r=0;r<300;r++){
    for(i=0;i<N;i++){
      ps[i].x=(ps[i].x+ps[i].v)%1000;
      ps[i].y=(ps[i].y+ps[i].x)%1000;
      ps[i].v=(ps[i].v+ps[i].y)%7+1;
    }
  }
  for(i=0;i<N;i++) chk=(chk*31+ps[i].x+ps[i].y*3+ps[i].v)%1000003;
  printf("%d\n",chk);
  return 0;
}
