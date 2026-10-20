#include <stdio.h>
unsigned int tab[256];
unsigned char data[20000];
int main(){
  unsigned int c,crc; int i,k,x=1;
  for(i=0;i<256;i++){ c=i; for(k=0;k<8;k++){ if(c&1) c=0xEDB88320u^(c>>1); else c=c>>1; } tab[i]=c; }
  for(i=0;i<20000;i++){ x=(x*131+17)%65521; data[i]=x&255; }
  crc=0xFFFFFFFFu;
  for(i=0;i<20000;i++) crc=tab[(crc^data[i])&255]^(crc>>8);
  crc=crc^0xFFFFFFFFu;
  printf("%u\n",crc);
  return 0;
}
