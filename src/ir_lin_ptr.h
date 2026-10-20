/* ir_lin_ptr.h — pointer/member/index lower helpers.
 * PERF: pure compile-time helpers; no hot-path getters.
 */
#ifndef BABYC_IR_LIN_PTR_H
#define BABYC_IR_LIN_PTR_H

#include "interpreter.h"
#include "ir_lin.h"
#include "ir_storage.h"

struct ValueType *IrLowerCallReturnStruct(IrExpr *Call);
struct ValueType *IrLowerCallReturnType(IrExpr *Call);
int IrResolveMemberLayout(struct ValueType *ST, const char *Name,
                          int *OffOut, int *EsOut, int *EbOut, int *IsPtrOut);
int IrLowerMemberOffEs(struct ValueType *ST, const char *Name,
                       int *OffOut, int *EsOut, int *EbOut);
struct ValueType *IrLowerPtrRowPointee(IrExpr *Row);
int IrLowerIndexRowIsPointer(IrExpr *Row);
int IrLowerInferAggregateSize(struct ValueType *T);
int LowerLocalStructArrayAddress(IrExpr *E, int Slot, int ElemBytes,
    IrLinInst *Code, int Cap, int *N, int *Next, int *Addr);
int LowerGlobalStructArrayAddress(IrExpr *E, IrExpr *Root, int ElemBytes,
    IrLinInst *Code, int Cap, int *N, int *Next, int *Addr);
struct ValueType *IrLowerCallLocalType(int slot);

int IrSafeMemberAddr(struct ParseState *Parser, IrExpr *E, void **Addr, int *Es, int *IsUnsigned);
int LinTryParamIndexedAccess(const IrLinInst *In, long long *R, int *Ip);
void IrLinCheckOwnedPointerAccess(void *Ptr, int Size, IrExpr *IX, int Mark);
#endif
