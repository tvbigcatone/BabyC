#ifndef BABYPOSIX_STAT_H
#define BABYPOSIX_STAT_H
#include "../interpreter.h"
extern const char BabyPosixStatDefs[];
extern struct LibraryFunction BabyPosixStatFunctions[];
void BabyPosixStatSetupFunc(Picoc *pc);
#endif
