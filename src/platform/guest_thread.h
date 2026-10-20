/* Shared educational trampoline: guest function + OS thread under one GIL.
 * Used by BabyPOSIX pthread_create and libWINAPI CreateThread so both layers
 * stay specular and the interpreter stays single-threaded for diagnostics. */
#ifndef BABYC_GUEST_THREAD_H
#define BABYC_GUEST_THREAD_H

#include "../interpreter.h"

void BabycGuestSetPC(Picoc *pc);
Picoc *BabycGuestGetPC(void);
void BabycGuestGIL_Lock(void);
void BabycGuestGIL_Unlock(void);
int BabycGuestGIL_UnlockIfHeld(void);
void BabycGuestGIL_RelockIf(int was_held);
struct Value *BabycGuestResolveFuncValue(struct Value *StartParam);
/* Call interpreted FuncValue with optional void* arg. Caller holds GIL. */
void *BabycGuestCall(struct Value *FuncValue, void *arg);

#endif

/* Call interpreted FuncValue with two void* args; return int result (compar).
 * Same trampoline as BabycGuestCall — used by qsort/bsearch. Caller on main thread. */
int BabycGuestCallInt2(struct Value *FuncValue, void *a, void *b);
