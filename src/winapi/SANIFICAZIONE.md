# Secondo round di sanificazione — audit iniziale

## Interventi eseguiti

- Aggiunto include guard a `wthreads.h` e rimosso il `;` spurio da `MAX_THREADS`.
- Reso `INVALID_HANDLE` un sentinel intero signed pointer-sized, coerente con gli usi interni basati su `intptr_t`; corretto il ritorno da `CreateThread` nel caso di inizializzazione fallita.
- Sostituito `readdir_r` (deprecata) con `readdir`, distinguendo fine directory da errore tramite `errno`.
- Rimosso una variabile inutilizzata in `fsearch_get`.
- Corretto il tipo della routine di avvio thread: ora conserva la firma Win32 (`DWORD (LPVOID)`) invece di reinterpretarla come `void (void *)`; il valore restituito viene salvato come codice di uscita.
- Corretto il fall-through nel caso di codice di uscita thread pari a zero.
- Sostituita la chiamata `fallocate` non dichiarata con `ftruncate` per dimensionare il backing file degli eventi nominati.
- Corretto l'argomento passato a `munmap` per gli eventi nominati.
- Aggiunto il test smoke al sistema CTest.

## Verifiche eseguite

- Build CMake predefinita: OK.
- Build CMake con `WINAPI_THREADS=ON` e `WINAPI_SHMEM=ON`: OK.
- Test `winapi_smoke`: OK (tipi fondamentali, `GetLastError` per-thread, tempo, contatore ad alta risoluzione e memoria).

## Problemi residui — non considerare la libreria già sicura

1. **Thread e handle:** le liste globali di thread/eventi/mutex non hanno ancora una strategia coerente di sincronizzazione, ownership e lifetime. Le chiamate concorrenti possono causare race, use-after-free e corruzione.
2. **WaitForMultipleObjects:** il modello basato su thread ausiliari e variabili condivise necessita di una riscrittura semantica e di test; non è garantita la compatibilità con le regole Win32 per wait-all, wait-any, timeout e handle non validi.
3. **Eventi nominati:** uso di file in `/tmp`, collisioni, permessi, cleanup e semantica interprocesso richiedono una progettazione dedicata. La correzione di `munmap` non risolve questi problemi.
4. **SuspendThread/TerminateThread:** l'uso di segnali per sospendere thread è fragile; terminare forzatamente un thread può lasciare mutex bloccati. Queste API non dovrebbero essere considerate affidabili.
5. **Memoria condivisa (`wshmem.c`):** mancano ancora verifiche sistematiche di allocazione, overflow, dimensioni, offset/allineamento, errori POSIX e rilascio delle risorse.
6. **File e percorsi (`wfiles.c`):** rimangono buffer fissi e copie/concatenazioni da controllare, registri di ricerca non protetti, conversioni di path da testare e gestione degli errori incompleta.
7. **Compatibilità ABI:** i tipi di base sono stati rivisti, ma strutture, costanti, calling convention, wide strings e differenze tra Windows 32/64 bit devono essere verificate API per API.
8. **API non implementate:** compilare con successo non significa che ogni funzione esportata sia implementata o abbia semantica Windows. Va definito un catalogo esplicito di supporto.

## Strategia consigliata

Procedere per moduli, con test mirati e sanitizzatori, mantenendo le funzionalità pericolose opzionali finché non superano i test. Prima dell'integrazione con BabyC, evitare macro che rinominano globalmente funzioni libc: registrare esplicitamente nell'interprete le API selezionate e definire quali simboli sono forniti dalla libc dell'host.


## C51 — FormatMessage

`FormatMessage` supporta soltanto messaggi di sistema nel buffer del chiamante. Non supporta `FORMAT_MESSAGE_ALLOCATE_BUFFER`, sorgenti personalizzate, inserimenti/formattazione di argomenti o selezione localizzata della lingua; i flag non supportati producono `ERROR_CALL_NOT_IMPLEMENTED` invece di fingere successo.

## C51 — diagnostica e helper stringhe

- `FormatMessage` ha un sottoinsieme esplicito: `FORMAT_MESSAGE_FROM_SYSTEM` con buffer fornito dal chiamante e `FORMAT_MESSAGE_IGNORE_INSERTS` opzionale. Non implementa allocazione buffer, sorgenti personalizzate o inserimenti; modalità non supportate falliscono esplicitamente.
- `str_replace` restituisce `NULL` per input stringa nullo, duplica la stringa se `substr`/`replacement` sono nulli, lascia invariata la stringa per `substr` vuoto e non rielabora il testo appena inserito.
- `RtlInitUnicodeString` azzera la struttura se la stringa sorgente è nulla.

## C53 — gestione errori e cleanup nelle ricerche CRT

- Il rilascio della lista drive è indipendente dallo stato di inizializzazione e gestisce anche liste parziali.
- `_findfirst` valida i puntatori, controlla le allocazioni del registro e libera la ricerca POSIX se non riesce a creare il record CRT.
- L'ordinamento della lista CRT non alloca più nodi temporanei per `.` e `..`, evitando una perdita di memoria.
- `GCC -fanalyzer -Werror` supera la build completa; regressioni per gli argomenti nulli di `_findfirst`.
