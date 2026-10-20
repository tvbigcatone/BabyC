/* ir.h — parser / executor separation (BabyC) */
#ifndef BABYC_IR_H
#define IR_MAX_ARRAY_DIMS 4
#define BABYC_IR_H
#define IR_TF_RESOLVED  0x01
#define IR_TF_FP        0x02
#define IR_TF_UNSIGNED  0x04
#define IR_TF_CONST     0x08
#define IR_TF_PTR       0x10
#define IR_TF_DEFINITELY_INIT 0x20
#define IR_TF_INT       0x40
#define IR_TF_FUSABLE   0x80
#define IR_CF_VALID     0x01
#define IR_CF_UNSIGNED  0x02
#define IR_CF_FP        0x04
#define IR_CF_PTR       0x08
#define IR_CF_WIDE      0x10
/* Point2: ForFpKind values (compile-time or first-probe). */
#define IR_FP_UNK              0
#define IR_FP_MISS             1
#define IR_FP_PTR_WALK         2
#define IR_FP_ITER_CALL_ACCUM  3
#define IR_FP_KERNEL_FOR       4
#define IR_FP_DENSE_BODY       5
#define IR_FP_ARRAY_ACCUM      6
#define IR_FP_ARRAY_FILL       7
#define IR_FP_LEAF_CALL_ACCUM  8
#define IR_FP_SCORE_CALL_ACCUM 9
#define IR_FP_TIGHT_FOR        10
#define IR_FP_NESTED_THIN      11
#define IR_FP_STRUCT_INLINE    12
#define IR_FP_THIN_IF_CALL     13
#define IR_FP_MATMUL          14
#define IR_VW_UNK  0
#define IR_VW_I8   1
#define IR_VW_I16  2
#define IR_VW_I32  4
#define IR_VW_I64  8
typedef enum { IrLoopFor=0, IrLoopWhile, IrLoopDo } IrLoopKind;
typedef struct IrCond { int Active; char *VarName; enum LexToken Op; int BoundIsVar; long long Bound; char *BoundVarName; int VarSlot; int BoundSlot; struct Value *CachedVar; struct Value *CachedBound; } IrCond;
typedef struct IrInc { int Active; char *VarName; int VarSlot; struct Value *CachedVar; } IrInc;
typedef enum { IrExConst=1, IrExIdent, IrExBinOp, IrExAssign, IrExCall, IrExUnary, IrExIndex, IrExMember, IrExTernary, IrExVaArg } IrExprKind;
typedef struct IrExpr {
  IrExprKind Kind; enum LexToken Op; long long ConstVal; char *Ident;
  int ParamSlot; int LocalSlot; struct IrExpr *Left; struct IrExpr *Right;
  struct IrExpr **Args; int ArgCount; int BindSlot; int I32;
  struct ParseState *Replay; int ReplayKind; int SourceLine; int CharacterPos;
  struct Value *CachedFunc; struct Value *CachedVal;
  void *CachedAddr; /* global array/scalar base; set by IrResolvePtr */
  unsigned char BitOffset, BitWidth, BitSigned, TypeFlags, ValWidth, SignedFast, WideCache; int ElemSize;
  struct ValueType *PointeeType; /* a[i] of T *a[N]: declared T (compile-time) */
  struct ValueType *VaType; /* requested type of va_arg(ap, T) */
  /* Canonical expression type, resolved once by IrAnnotateExpr after children. */
  int CanonicalBase; unsigned char CanonicalFlags;
  int CanonicalOpBase; unsigned char CanonicalOpFlags;
  /* Exact struct/union type identity when CanonicalBase is an aggregate
   * (NULL otherwise); resolved together with CanonicalBase. */
  struct ValueType *CanonicalType;
} IrExpr;
typedef enum { IrStExpr=1, IrStBlock, IrStIf, IrStReturn, IrStBreak, IrStContinue, IrStWhile, IrStDo, IrStFor, IrStDecl, IrStDelegate, IrStSwitch, IrStCase, IrStDefault, IrStGoto, IrStLabel } IrStmtKind;
typedef struct IrStmt {
  IrStmtKind Kind; IrExpr *Expr; struct IrStmt *ThenStmt, *ElseStmt, **Stmts; int StmtCount;
  struct IrStmt *BodyStmt, *InitStmt, *IncStmt; IrCond FastCond; IrInc FastInc; signed char ForFpKind; signed char FpTouch; /* cached FP-touch classification: 0 unknown, 1 integer-only, 2 FP */
  char *DeclIdent; void *DeclSlot; int LocalSlot; int DeclIsPtr; struct ValueType *DeclFromType;
  int DeclIsFptr, DeclElemIsPtr, DeclIsStatic, DeclIsConst, DeclElemSize, DeclPointeeSize, DeclArraySize, DeclNDims;
  struct ValueType *DeclElemPointee; /* T *a[N]: T (pointee of each element) */
  int DeclDims[IR_MAX_ARRAY_DIMS]; long long *DeclInitVals; int DeclInitCount; IrExpr **DeclInitExprs; char **DeclInitNames;
  int SourceLine, CharacterPos; const char *SourceFile; struct ParseState *Resume;
} IrStmt;
typedef struct IrLoop { IrLoopKind Kind; IrCond Cond; IrInc Inc; void *BodyTokens; IrStmt *BodyIr; struct ParseState BodyTemplate, CondFrom; int CondNeedsCloseBracket; } IrLoop;
void IrFreeFunctionBody(void *BodyIr);
void IrCanonicalCheckNode(IrExpr *E);
extern int IrCanonicalGateEnabled;
void IrExecuteFunction(struct ParseState *Parser, struct Value *FuncValue, struct Value **ParamArray, int ArgCount);
struct IrStmt *IrCompileBody(struct ParseState *BodyStart, struct ParseState *BodyEnd);
void IrVerifyBody(struct IrStmt *Body, const char *FuncHint);
void IrCompileSetParamContext(char **ParamNames, struct ValueType **ParamTypes, int NumParams);
void IrCompileClearParamContext(void);
void *IrCompileFunctionBody(struct ParseState *BodyStart);
extern int PicocIrBodyCompiled, PicocIrBodyTokenFallback;
void BabycAnalyzeReset(void); int BabycAnalyzeGetCount(void);
void BabycAnalyzeGet(int i, const char **Name, const char **File, int *Line, int *IsIr);
void BabycAnalyzeGetWhy(int i, const char **Why);
void BabycAnalyzeNote(const char *Name, const char *File, int Line, int IsIr);
const char *IrCompileGetWhy(void);

/* Point3b: compile-time frame shape for BodyIr (NumLocals/params + slot kinds). */
#define IR_FRAME_SLOT_UNK   0
#define IR_FRAME_SLOT_INT   1
#define IR_FRAME_SLOT_PTR   2
#define IR_FRAME_SLOT_FP    3
void IrBodyFrameRegister(void *Body, int NumLocals, int NumParams);
int IrBodyFrameLookup(void *Body, short *NumLocalsOut, short *NumParamsOut,
                      unsigned char *SlotKind /*[128] or NULL*/,
                      unsigned char *SlotWidth /*[128] or NULL*/);

#endif
