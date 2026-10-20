# BabyPOSIX — Step 16: riuso dei descriptor e reset dei flag

**Data:** 2026-10-14  
**Base di lavoro:** archivio corrente fornito dall'utente `babyc(4).zip`  
**Delta cumulativo di consegna:** confrontato con la baseline `babyc(3).zip`

## Obiettivo

Aggiungere una regressione mirata al ciclo di vita dei descriptor guest. I test esistenti coprivano l'esaurimento della tabella e `FD_CLOEXEC`, ma non verificavano in un singolo scenario che un descriptor chiuso, poi riutilizzato da `open()` o `dup()`, non conservasse flag appartenenti alla precedente occupazione dello slot.

## Modifiche

- Aggiunto `src/tests/posix/fd_reuse.c`.
- Verificati i seguenti contratti:
  1. `dup()` crea un descriptor distinto e non eredita `FD_CLOEXEC` dal sorgente.
  2. `open()` riutilizza lo slot libero più basso.
  3. Uno slot riutilizzato da `open()` parte con `F_GETFD == 0`.
  4. Dopo `dup2()` e `close()` di un descriptor di destinazione, il descriptor chiuso è invalido.
  5. `dup()` riutilizza lo slot libero più basso e azzera i flag del nuovo descriptor.
- Il runner `src/tests/run-posix-tests-msvc.bat` già seleziona `posix\*.c`, quindi il nuovo test viene incluso automaticamente senza modifiche al batch.

## Verifiche eseguite qui

- Build Linux/GCC della sorgente corrente: **PASS**.
- Suite POSIX: **13/13 PASS** in modalità predefinita.
- Suite POSIX: **13/13 PASS** con `BABYC_EXEC=vm`.
- Suite POSIX: **13/13 PASS** con `BABYC_EXEC=both`.

**Limite di copertura:** nei test lanciati con `BABYC_EXEC=both` i contatori del bytecode compatto sono rimasti `runs=0`; questi risultati confermano il comportamento dei test nel percorso selezionato/fallback, non una copertura diretta del bytecode compatto. Non è stata eseguita una build o una suite Windows/MSVC in questo ambiente.

## Verifica richiesta nell'ambiente MSVC

Dalla directory `src\tests` del progetto Windows, ricompilare la soluzione se necessario e lanciare:

```bat
run-posix-tests-msvc.bat
```

Risultato atteso per il nuovo caso:

```text
==== BabyPOSIX test: posix\fd_reuse.c ====
BabyPOSIX fd reuse: ok
```

Lo Step 16 amplia la copertura di regressione; non dichiara nuove API POSIX implementate né sostituisce la verifica Windows.
