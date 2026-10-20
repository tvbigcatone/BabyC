#ifndef BABYC_POSIX_TIME_H
#define BABYC_POSIX_TIME_H

#include "../interpreter.h"

extern struct LibraryFunction BabyPosixTimeFunctions[];
extern const char BabyPosixTimeDefs[];
void BabyPosixTimeSetupFunc(Picoc *pc);
extern struct LibraryFunction BabyPosixTimesFunctions[];
extern const char BabyPosixTimesDefs[];
void BabyPosixTimesSetupFunc(Picoc *pc);

#endif
