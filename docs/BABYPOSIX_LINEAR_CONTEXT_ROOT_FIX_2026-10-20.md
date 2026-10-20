# BabyPOSIX / Linear STRICT: uscita non locale — 2026-10-20

## Causa radice

Il cleanup precedente ripristinava la profondità dei frame Linear e IR, ma non lo stato globale usato dagli epiloghi normali di `FnCodeRun`. `pthread_exit()` salta tali epiloghi e lasciava `LinParser` puntare al parser locale del worker ormai terminato; inoltre potevano restare i flag/contatori Linear e `CallSkip` del worker. Il thread veniva joinato e il valore di uscita scritto correttamente, ma il chiamante poteva crashare al primo accesso successivo al codice interpretato.

## Correzione

Aggiunti `IrLinThreadContextCapture/Restore` per salvare/ripristinare solo il parser, i flag/contatori Linear, il valore di ritorno e `CallSkip[128]` ai confini dell'uscita non locale. Il ripristino avviene solo in `pthread_exit`/uscita guest con `longjmp`; non aggiunge copie al percorso ordinario per chiamata o per istruzione.

## Verifiche locali

Build Clang 17 `-O0` riuscita. La matrice mirata (4 test × 3 modalità) è 12/12; `pthread_exit_retval.c` è passato 10/10 volte in Linear STRICT. La suite dual completa locale è terminata al limite d'esecuzione senza riepilogo, quindi non è dichiarata superata.
