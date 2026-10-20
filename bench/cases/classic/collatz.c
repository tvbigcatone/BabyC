#include <stdio.h>
int main(){
  int n,x,steps,total=0,best=0;
  for(n=1;n<8000;n++){
    x=n; steps=0;
    while(x!=1){ if(x%2==0) x=x/2; else x=3*x+1; steps++; }
    total+=steps;
    if(steps>best) best=steps;
  }
  printf("%d %d\n",total,best);
  return 0;
}
