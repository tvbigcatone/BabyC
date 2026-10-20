#include <stdio.h>
#define N 400
struct P { int x; int y; int v; };
struct P ps[N];
void upd(struct P *p){
  p->x=(p->x+p->v)%1000;
  p->y=(p->y+p->x)%1000;
  p->v=(p->v+p->y)%7+1;
}
int main(){
  int i,r,chk=0;
  for(i=0;i<N;i++){ ps[i].x=i; ps[i].y=N-i; ps[i].v=1; }
  for(r=0;r<150;r++) for(i=0;i<N;i++) upd(&ps[i]);
  for(i=0;i<N;i++) chk=(chk*31+ps[i].x+ps[i].y*3+ps[i].v)%1000003;
  printf("%d\n",chk);
  return 0;
}
