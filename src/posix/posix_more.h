#ifndef BABYPOSIX_MORE_H
#define BABYPOSIX_MORE_H
#include "../interpreter.h"
extern const char BabyPosixSelectDefs[];
extern struct LibraryFunction BabyPosixSelectFunctions[];
void BabyPosixSelectSetupFunc(Picoc *pc);
extern const char BabyPosixPollDefs[];
extern struct LibraryFunction BabyPosixPollFunctions[];
void BabyPosixPollSetupFunc(Picoc *pc);
extern const char BabyPosixResourceDefs[];
extern struct LibraryFunction BabyPosixResourceFunctions[];
void BabyPosixResourceSetupFunc(Picoc *pc);
extern const char BabyPosixStringsDefs[];
extern struct LibraryFunction BabyPosixStringsFunctions[];
void BabyPosixStringsSetupFunc(Picoc *pc);
extern const char BabyPosixLocaleDefs[];
extern struct LibraryFunction BabyPosixLocaleFunctions[];
void BabyPosixLocaleSetupFunc(Picoc *pc);
extern const char BabyPosixInttypesDefs[];
extern struct LibraryFunction BabyPosixInttypesFunctions[];
void BabyPosixInttypesSetupFunc(Picoc *pc);
extern const char BabyPosixStddefDefs[];
extern struct LibraryFunction BabyPosixStddefFunctions[];
void BabyPosixStddefSetupFunc(Picoc *pc);
extern const char BabyPosixParamDefs[];
extern struct LibraryFunction BabyPosixParamFunctions[];
void BabyPosixParamSetupFunc(Picoc *pc);
extern const char BabyPosixSysmacrosDefs[];
extern struct LibraryFunction BabyPosixSysmacrosFunctions[];
void BabyPosixSysmacrosSetupFunc(Picoc *pc);
extern const char BabyPosixFileDefs[];
extern struct LibraryFunction BabyPosixFileFunctions[];
void BabyPosixFileSetupFunc(Picoc *pc);
extern const char BabyPosixFnmatchDefs[];
extern struct LibraryFunction BabyPosixFnmatchFunctions[];
void BabyPosixFnmatchSetupFunc(Picoc *pc);
extern const char BabyPosixSchedDefs[];
extern struct LibraryFunction BabyPosixSchedFunctions[];
void BabyPosixSchedSetupFunc(Picoc *pc);
#endif
