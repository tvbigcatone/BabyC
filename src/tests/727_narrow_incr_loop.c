/* BUG IR (W2,W3,W4): ++/--/a++ su tipi a 8/16 bit non troncano. gcc/vanilla=44, BabyC=300. NO_IR=1 corretto. */
#include <stdio.h>
int main(){ unsigned char a=0; int i; for(i=0;i<300;i++) a++; printf("%d\n",(int)a); return 0; }
