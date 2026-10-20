#ifndef BABYPOSIX_UNISTD_H
#define BABYPOSIX_UNISTD_H

#include "../interpreter.h"

extern const char BabyPosixUnistdDefs[];
extern struct LibraryFunction BabyPosixUnistdFunctions[];
void BabyPosixUnistdSetupFunc(Picoc *pc);

#endif
