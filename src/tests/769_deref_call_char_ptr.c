/* W6+: *f() / f()[0] must use return pointee width (char/short/uchar). */
#include <stdio.h>
char *sp(char *p){ return p+1; }
short *sp_s(short *p){ return p+1; }
unsigned char *sp_uc(unsigned char *p){ return p+1; }
int main(void)
{
    char s[8]="hello";
    short a[4]={10,20,30,40};
    unsigned char b[4]={1,2,3,4};
    int t=*sp(s);
    int u=sp(s)[0];
    int v=*sp_s(a);
    int w=sp_uc(b)[0];
    printf("%d %d %d %d\n",t,u,v,w);
    return 0;
}
