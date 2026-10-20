#include <stdio.h>
int main(void){ int x=7,y=9; int b[3]={x,y,x+y}; int c[2][2]={{x,1},{y,x*2}}; printf("%d %d %d | %d %d %d %d\n",b[0],b[1],b[2],c[0][0],c[0][1],c[1][0],c[1][1]); return 0;}
