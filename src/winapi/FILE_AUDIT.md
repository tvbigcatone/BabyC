# Fase B — Audit file, percorsi e handle di ricerca

## Correzioni in questo round

- Verifica degli argomenti nulli nelle API di ricerca file.
- Gestione esplicita di fallimenti di allocazione e codici di errore di `glob`.
- Nome restituito in `WIN32_FIND_DATA.cFileName` limitato al basename e controllato rispetto alla capacità del campo.
- Correzione dell'indice della ricerca: `FindFirstFile` consuma il primo risultato, quindi `FindNextFile` inizia dal successivo.
- Attributi base e dimensione file inizializzati a partire da `stat`.
- `GetLogicalDriveStrings` verifica la capacità del buffer prima di scrivere la multistringa.
- `GetCurrentDirectory` non scrive nel buffer se la capacità è insufficiente e restituisce la lunghezza richiesta comprensiva del terminatore in quel caso.
- `GetDiskFreeSpaceEx` rispetta i puntatori di output opzionali previsti dalla firma e usa la directory corrente se il percorso è nullo.
- Migliorata la gestione degli errori in `update_drive_list` e aggiunti controlli sui percorsi costruiti.

## Test eseguiti

- Build CMake standard.
- Build con `WINAPI_THREADS=ON` e `WINAPI_SHMEM=ON`.
- CTest in entrambe le configurazioni.
- Test di regressione su `_findfirst`/`_findnext`/`_findclose`, `GetCurrentDirectory` con buffer insufficiente e `GetDiskFreeSpaceEx` con output nulli.

## Rischi residui

1. La lista globale usata da `FindFirstFile` non è sincronizzata: chiamate concorrenti possono interferire.
2. Gli handle sono puntatori opachi e la lista non elimina il rischio di riuso di indirizzi dopo la chiusura.
3. `FILETIME` di `WIN32_FIND_DATA` non viene ancora convertito correttamente dai timestamp POSIX.
4. La rappresentazione dei drive Windows tramite mount point Linux è necessariamente approssimata.
5. `_makepath`, `_splitpath`, `win2posix_path` e `posix2win_path` conservano casi limite da analizzare separatamente.
6. La semantica degli errori e i flag di enumerazione non sono ancora equivalenti a Win32.

Questa fase migliora la robustezza di base ma non certifica la compatibilità completa.

Il test di regressione è stato inoltre eseguito con AddressSanitizer e UndefinedBehaviorSanitizer, con rilevamento delle perdite abilitato: esito positivo sul test di fumo corrente.

## Round aggiuntivo — conversioni dei percorsi

- Riscritte `win2posix_path` e `posix2win_path` per controllare gli argomenti nulli, gestire esplicitamente gli errori di allocazione e verificare i limiti di dimensione prima di costruire i risultati.
- La conversione da POSIX a Windows ora riconosce i mount point solo quando coincidono con un componente completo del percorso (evita, per esempio, che `/media/disk2` venga associato erroneamente al mount point `/media/disk`).
- La conversione dei separatori avviene in-place, evitando cicli di riallocazione per comprimere separatori ripetuti.
- `_splitpath` ora gestisce `path == NULL` senza dereferenziare il puntatore e salta sempre il prefisso drive nei componenti directory/nome, anche quando il buffer `drv` è nullo.
- Aggiunti test per separatori misti, input nulli e scomposizione di un percorso con drive ed estensione composta.

### Limiti residui

- La corrispondenza tra drive Windows e dispositivi Linux è euristica e non rappresenta in modo affidabile tutte le installazioni, soprattutto container, mount bind, filesystem di rete e sistemi senza dispositivi a blocchi visibili.
- Le funzioni `_makepath` e `_splitpath` seguono un'interfaccia legacy senza argomenti di capacità dei buffer: non è possibile garantire la sicurezza della destinazione se il chiamante non riserva spazio sufficiente. Prima di usarle con input non fidati serve introdurre varianti con dimensione esplicita, mantenendo quelle legacy solo per compatibilità.
- Le chiamate concorrenti a `FindFirstFile`/`FindNextFile`/`FindClose` continuano a richiedere sincronizzazione della lista globale degli handle; questo sarà affrontato nella revisione generale degli handle.

## C52 — matcher wildcard e fallimenti di allocazione

- Il matcher CRT è ora iterativo e null-safe; la regressione include un pattern deliberatamente avversario per proteggere contro ricorsione profonda/backtracking patologico.
- `_findfirst`/`_findnext` resta un sottosistema CRT separato. Il fallimento dell'allocazione della copia del pattern in `findfirst_in_directory` viene gestito esplicitamente e il percorso senza primo risultato libera tutte le risorse.
- La build standard e quella completa passano con `-Werror`; ASan+UBSan e 30 ripetizioni della suite passano. L'analisi GCC `-fanalyzer` resta aperta per il ciclo di vita della lista temporanea dei drive in `is_win_path`/`update_drive_list`.
