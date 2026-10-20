#ifndef BABYPOSIX_DIRENT_H
#define BABYPOSIX_DIRENT_H
#include "../interpreter.h"
extern const char BabyPosixDirentDefs[];
extern struct LibraryFunction BabyPosixDirentFunctions[];
void BabyPosixDirentSetupFunc(Picoc *pc);
#endif
