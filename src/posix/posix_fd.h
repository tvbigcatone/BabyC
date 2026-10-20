#ifndef BABYPOSIX_FD_H
#define BABYPOSIX_FD_H
#include "../interpreter.h"
#define BABYPOSIX_FD_CAPACITY 256
#define BP_FD_READ  1
#define BP_FD_WRITE 2
extern const char BabyPosixFcntlDefs[];
extern struct LibraryFunction BabyPosixFcntlFunctions[];
void BabyPosixFcntlSetupFunc(Picoc *pc);
void BabyPosixReadCall(struct ParseState *, struct Value *, struct Value **, int);
void BabyPosixWriteCall(struct ParseState *, struct Value *, struct Value **, int);
void BabyPosixCloseCall(struct ParseState *, struct Value *, struct Value **, int);
void BabyPosixDupCall(struct ParseState *, struct Value *, struct Value **, int);
void BabyPosixDup2Call(struct ParseState *, struct Value *, struct Value **, int);
void BabyPosixLseekCall(struct ParseState *, struct Value *, struct Value **, int);
void BabyPosixPipeCall(struct ParseState *, struct Value *, struct Value **, int);
void BabyPosixPipe2Call(struct ParseState *, struct Value *, struct Value **, int);
void BabyPosixDup3Call(struct ParseState *, struct Value *, struct Value **, int);
void BabyPosixPreadCall(struct ParseState *, struct Value *, struct Value **, int);
void BabyPosixPwriteCall(struct ParseState *, struct Value *, struct Value **, int);
void BabyPosixFcntlCall(struct ParseState *, struct Value *, struct Value **, int);
int BabyPosixFdNativeOf(int guest_fd);
int BabyPosixFdAllocate(int native_fd, int access, int owns_native_fd);
int BabyPosixFdNativeOfDir(int dirfd);
void BabyPosixFdRelease(int guest_fd);
void BabyPosixFdDisown(int guest_fd);
#endif
