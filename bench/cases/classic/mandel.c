#include <stdio.h>
int main(){
  int px,py,it,inside=0;
  double x0,y0,x,y,xt;
  for(py=0;py<80;py++) for(px=0;px<120;px++){
    x0=px*3.5/120.0-2.5; y0=py*2.0/80.0-1.0;
    x=0.0;y=0.0;
    for(it=0;it<60 && x*x+y*y<=4.0;it++){ xt=x*x-y*y+x0; y=2.0*x*y+y0; x=xt; }
    if(it==60) inside++;
  }
  printf("%d\n",inside);
  return 0;
}
