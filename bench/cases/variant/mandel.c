#include <stdio.h>
int iter(double cr,double ci){
  double zr=0.0,zi=0.0,t;
  int n=0;
  while(n<50 && zr*zr+zi*zi<=4.0){ t=zr*zr-zi*zi+cr; zi=2.0*zr*zi+ci; zr=t; n++; }
  return n;
}
int main(){
  int x,y,sum=0;
  for(y=0;y<60;y++) for(x=0;x<90;x++) sum+=iter(x*3.0/90.0-2.0, y*2.4/60.0-1.2);
  printf("%d\n",sum);
  return 0;
}
