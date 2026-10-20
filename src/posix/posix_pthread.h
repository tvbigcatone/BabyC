#ifndef BABYPOSIX_PTHREAD_H
#define BABYPOSIX_PTHREAD_H

#include "../interpreter.h"

/* BabyPOSIX pthread subset — educational thin layer.
 *
 * Part of the bidirectional desktop compatibility pair:
 *   BabyPOSIX  (#include <pthread.h>, <unistd.h>, …) on every host
 *   libWINAPI  (#include <windows.h>) on every host
 * Both are omitted from BABYC_MICRO builds.
 *
 * On UNIX: thin use of system libpthread for mutex/cond.
 * On WIN32: CriticalSection / ConditionVariable so a Windows student can run
 * simple pthreads exercises under the interpreter (with the same friendly
 * diagnostics as the rest of BabyC).
 *
 * Step 1 surface:
 *   - types/constants for pthread_t, mutex, cond, once, attr
 *   - pthread_mutex_* and pthread_cond_* for the documented subset
 *   - pthread_once, pthread_self, pthread_equal
 *   - pthread_create uses the shared GIL trampoline (same as CreateThread).
 *     Blocking lock/wait/join drop the GIL so the peer guest thread can run.
 */

extern const char BabyPosixPthreadDefs[];
extern struct LibraryFunction BabyPosixPthreadFunctions[];
void BabyPosixPthreadSetupFunc(Picoc *pc);

#endif
