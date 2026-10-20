/* BUG (nuova versione): segfault. int* da malloc scritto con *p=..., poi free() nella stessa funzione.
   Atteso: ok.  Nuova: SIGSEGV (PIE) / passa per caso (no-PIE).  Vecchia e vanilla: ok.
   Con BABYC_NO_IR=1 passa. In IrFuseWriteDerefInt (ir_exec.c ~4793) Addr e' allineato a pagina,
   non e' il puntatore restituito da malloc. */
#include <stdio.h>
#include <stdlib.h>
int main(){ int *p; p=malloc(4); *p=1; free(p); printf("ok\n"); return 0; }
