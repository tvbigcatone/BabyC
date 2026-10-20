# Delta cumulativo libWINAPI — semantica EOF di ReadFile (2026-10-19)

Baseline iniziale: `babyc_baseline_2026-10-19_phase2t1.zip`  
SHA-256 baseline: `d4c4ee298112801b1e221463dafa6d0c526cf8cc5dce9614f14a66d8ce7b5e9f`

Questo archivio è un overlay cumulativo rispetto alla baseline iniziale, non soltanto rispetto al delta del turno precedente. I percorsi nell'archivio iniziano con `babyc/`. Sono inclusi soltanto file nuovi o modificati; nessun file viene eliminato. Non sono inclusi `src/posix`, progetti MSVC o file numerati di test.

## Intervento di questo turno

`ReadFile` sincrono su file regolari ora restituisce `TRUE` quando raggiunge EOF e imposta il conteggio a zero. È la semantica Win32 attesa per la lettura sincrona: i chiamanti possono terminare il ciclo quando la chiamata ha successo ma non trasferisce byte. Il test di fumo è stato aggiornato e il worker della regressione concorrente tratta il conteggio zero come EOF valido. I percorsi overlapped restano esplicitamente non supportati.

## File nel delta cumulativo e SHA-256 prima → dopo

| File | Prima | Dopo |
|---|---|---|
| `babyc/src/winapi/STATUS.md` | `66bf35b5242e428b4b4e08184e8a02057bbfb621a59960c7705db7203815ab00` | `dfc2861e546b6390b41eb5307c15ef90c11ce3172eefe96ce2e988c65467daa4` |
| `babyc/src/cstdlib_ext/win.c` | `b759984c1d45ed4ead7e9d8633a824f2bf7ceb65da112854c99bfd59a91f3fdc` | `2e476423b3f13ca8f07358c95a51855243351986d051e0c25241cc91cbd99463` |
| `babyc/src/winapi/src/wthreads.c` | `84ffce4efbd6efc605ed53230a21ea520ddd00389b40e71f4a2666b8c5ecd0e0` | `4f57461884a1c6fea2fedb1575aaf86fcd98d1791f014f3ef31f08bd86ccaddb` |
| `babyc/src/winapi/src/wfile_handles.c` | `18dee4a09640c13ccfc3d4179791e427431bf4bdf6386e2d24bb013fee975a0c` | `a20296e7f925a9b71597410b1dbc008bf6ecb5fd6b23c23e4ac65c34f0413a2f` |
| `babyc/src/winapi/src/wprivate.h` | `ee26564cfbe909ef1ec9bcdee88b3d2f344193d3bdb6ea7761a74a7ecf1fee8e` | `63ed195b413b77f95d6d75bc580f1f46b2aa23d5040228587f67ec6c9968d9a4` |
| `babyc/src/winapi/src/wfiles.c` | `5c620eeaa204af027c3c594e2874434b9cf48e9fb495a6cc6d4cfb4d647df646` | `1a3c1767a813a14b5d7ff4c403e79d219664c5fbbb1ed56cd21eb975f765a1c5` |
| `babyc/src/winapi/src/wtime.c` | `5abffe81dce70d465c51bc2bf40acfeb06fecd15fd1c39b3bd3243b5b64a7f17` | `5d015d1ee8931762ec851ff266c9ff58023f2792fd52df346ba381096b3b2006` |
| `babyc/src/winapi/tests/smoke.c` | `4ac577ab90c435b7be4a44b7b15dcc9c736c992a4b06189b74a7337d3a5dcbc3` | `7d3d34986808eb60dc66870db65ae9b55b1c62b3052b9c7f8b8bb5ec55afc724` |
| `babyc/src/winapi/include/wfiles.h` | `0302d96f7884a13a4d5309df1ba20d803d37bcd317ac966482b16f408bf7fd49` | `6eebcac9beddd730114ac135325b624851c226ae1668a6b0caf543f04e8e231a` |
| `babyc/src/winapi/include/wtime.h` | `2784a0e56b1e26f64612d347c930a1e49485d1e6ae051eb3cd03b09f0f08024d` | `8795f02036cc3a71a02dbed9b617160c6640d5db54e1ffec0ac5bbba69cd9a9f` |
| `babyc/docs/DELTA_MANIFEST_LIBWINAPI_FILETIME_2026-10-19.md` | `(assente)` | `c4c39b1313dfa0482be059960f012185a532c547e05f51ff510a25194be6f833` |
| `babyc/src/tests/windows/winapi_filetime.c` | `(assente)` | `ca6622928f53825f7d69537be386e3462b5088010b8b68b2d6ec625422f66ab6` |

## Verifiche eseguite in questo turno

- `cmake --build src/winapi/build-filetime-followup -j2`: superata.
- `ctest --test-dir src/winapi/build-filetime-followup --output-on-failure`: 1/1 superato.
- `cmake --build src/winapi/build-full-followup -j2` e CTest: superati, 1/1.
- Build ASan+UBSan `build-filetime-asan` e CTest con `ASAN_OPTIONS=detect_leaks=1`: superati, 1/1.
- La verifica nativa MSVC/Windows resta da eseguire esternamente dall'utente.

## Confini rispettati

Questo intervento riguarda esclusivamente libWINAPI e i suoi test di fumo/documentazione; non modifica BabyPOSIX (`src/posix`), parser/interprete, test numerati o progetti MSVC.
