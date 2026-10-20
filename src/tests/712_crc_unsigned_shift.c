/* BUG (vecchia E nuova): risultato sbagliato in silenzio.
   Atteso: 2367727370.  BabyC: 1927259914.  Vanilla e gcc: corretto.
   BABYC_NO_IR=1 -> corretto.  Con un printf nel corpo del ciclo -> corretto.
   Diverge all'iterazione 4, quando crc supera 2^31. */
#include <stdio.h>
unsigned int tab[256];
int main(){
  unsigned int c,crc; int i,k;
  for(i=0;i<256;i++){ c=i; for(k=0;k<8;k++){ if(c&1) c=0xEDB88320u^(c>>1); else c=c>>1; } tab[i]=c; }
  crc=0xFFFFFFFFu;
  for(i=0;i<5;i++) crc=tab[(crc^(i*7))&255]^(crc>>8);
  printf("%u\n",crc);
  return 0;
}
