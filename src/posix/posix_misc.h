#ifndef BABYPOSIX_MISC_H
#define BABYPOSIX_MISC_H
#include "../interpreter.h"
extern const char BabyPosixGetoptDefs[];
extern struct LibraryFunction BabyPosixGetoptFunctions[];
void BabyPosixGetoptSetupFunc(Picoc *pc);
extern const char BabyPosixLibgenDefs[];
extern struct LibraryFunction BabyPosixLibgenFunctions[];
void BabyPosixLibgenSetupFunc(Picoc *pc);
extern const char BabyPosixUtimeDefs[];
extern struct LibraryFunction BabyPosixUtimeFunctions[];
void BabyPosixUtimeSetupFunc(Picoc *pc);
extern const char BabyPosixUtsnameDefs[];
extern struct LibraryFunction BabyPosixUtsnameFunctions[];
void BabyPosixUtsnameSetupFunc(Picoc *pc);
extern const char BabyPosixIoctlDefs[];
extern struct LibraryFunction BabyPosixIoctlFunctions[];
void BabyPosixIoctlSetupFunc(Picoc *pc);
extern const char BabyPosixTermiosDefs[];
extern struct LibraryFunction BabyPosixTermiosFunctions[];
void BabyPosixTermiosSetupFunc(Picoc *pc);
#endif
