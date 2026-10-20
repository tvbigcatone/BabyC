#include <stdio.h>
int main() {
  int a[2][3][4];
  int i,j,k,s=0;
  for(i=0;i<2;i++)
    for(j=0;j<3;j++)
      for(k=0;k<4;k++)
        a[i][j][k] = i*100+j*10+k;
  for(i=0;i<2;i++)
    for(j=0;j<3;j++)
      for(k=0;k<4;k++)
        s += a[i][j][k];
  printf("%d %d %d\n", a[0][0][0], a[1][2][3], s);
  return 0;
}
