/* picoc data type module. This manages a tree of data types and has facilities
 * for parsing data types. */
 
#include "interpreter.h"
#include <stddef.h>

/* some basic types */
static int PointerAlignBytes;
static int IntAlignBytes;
static int TypeStorageAlign(const struct ValueType *Typ);


/* add a new type to the set of types we know about */
struct ValueType *TypeAdd(Picoc *pc, struct ParseState *Parser, struct ValueType *ParentType, enum BaseType Base, int ArraySize, const char *Identifier, int Sizeof, int AlignBytes)
{
    struct ValueType *NewType = VariableAlloc(pc, Parser, sizeof(struct ValueType), TRUE);
    NewType->Base = Base;
    NewType->ArraySize = ArraySize;
    NewType->Sizeof = Sizeof;
    NewType->AlignBytes = AlignBytes;
    NewType->FPFormat = FPStorageNone;
    NewType->Identifier = Identifier;
    NewType->Members = NULL;
    NewType->FromType = ParentType;
    NewType->DerivedTypeList = NULL;
    NewType->OnHeap = TRUE;
    NewType->Next = ParentType->DerivedTypeList;
    ParentType->DerivedTypeList = NewType;
    
    return NewType;
}

/* given a parent type, get a matching derived type and make one if necessary.
 * Identifier should be registered with the shared string table. */
struct ValueType *TypeGetMatching(Picoc *pc, struct ParseState *Parser, struct ValueType *ParentType, enum BaseType Base, int ArraySize, const char *Identifier, int AllowDuplicates)
{
    int Sizeof;
    int AlignBytes;
    struct ValueType *ThisType = ParentType->DerivedTypeList;
    /* Must match ParentType (FromType): otherwise int a[2] and
     * int (*tab[2])(int) share one TypeArray and element type is wrong. */
    while (ThisType != NULL
           && (ThisType->Base != Base
               || ThisType->ArraySize != ArraySize
               || ThisType->Identifier != Identifier
               || ThisType->FromType != ParentType))
        ThisType = ThisType->Next;
    
    if (ThisType != NULL)
    {
        if (AllowDuplicates)
            return ThisType;
        else
            ProgramFail(Parser, "data type '%s' is already defined", Identifier);
    }
        
    switch (Base)
    {
        case TypePointer:   Sizeof = sizeof(void *); AlignBytes = PointerAlignBytes; break;
        case TypeArray:
            Sizeof = ArraySize * (ParentType->Base == TypeFP ? TypeFPStorageSize(ParentType) : ParentType->Sizeof);
            AlignBytes = TypeStorageAlign(ParentType);
            break;
        case TypeEnum:      Sizeof = sizeof(int); AlignBytes = IntAlignBytes; break;
        default:            Sizeof = 0; AlignBytes = 0; break;      /* structs and unions will get bigger when we add members to them */
    }

    return TypeAdd(pc, Parser, ParentType, Base, ArraySize, Identifier, Sizeof, AlignBytes);
}

/* stack space used by a value */
int TypeStackSizeValue(struct Value *Val)
{
    if (Val != NULL && Val->ValOnStack)
        return TypeSizeValue(Val, FALSE);
    else
        return 0;
}

/* memory used by a value */
int TypeSizeValue(struct Value* Val, int Compact)
{
    if (IS_INTEGER_NUMERIC(Val) && !Compact)
    {
        // long long e unsigned long long richiedono almeno 8 byte
        // anche su piattaforme 32-bit dove sizeof(ALIGN_TYPE) = 4
        int BaseSize = Val->Typ->Sizeof;
        int AlignSize = sizeof(ALIGN_TYPE);
        return (BaseSize > AlignSize) ? MEM_ALIGN(BaseSize) : AlignSize;
    }
    else if (Val->Typ->Base != TypeArray)
    {
#ifndef NO_FP
        /* FP values are evaluated in the interpreter's double slot even when
         * their C object representation is binary32. Lvalues backed by real
         * storage keep the object width; rvalues/proxies need the execution
         * width so stack frames and return slots cannot truncate a double. */
        if (Val->Typ->Base == TypeFP && Val->Typ->FPFormat == FPStorageBinary32
            && Val->ValRaw == NULL)
            return (int)sizeof(double);
#endif
        return Val->Typ->Base == TypeFP ? TypeFPStorageSize(Val->Typ) : Val->Typ->Sizeof;
    }
    else
    {
        struct ValueType *ElemType = Val->Typ->FromType;
        int ElemSize = ElemType->Base == TypeFP ? TypeFPStorageSize(ElemType) : ElemType->Sizeof;
        return ElemSize * Val->Typ->ArraySize;
    }
}

/* Return the storage properties associated with an FP type. Keep this
 * separate from sizeof(double): execution temporaries may still use double
 * while an object has binary32 storage. */
int TypeFPStorageSize(const struct ValueType *Typ)
{
    if (Typ == NULL)
        return 0;
    switch (Typ->FPFormat)
    {
        case FPStorageBinary32: return (int)sizeof(float);
        case FPStorageBinary64: return (int)sizeof(double);
        default: return Typ->Sizeof;
    }
}

int TypeFPStorageAlign(const struct ValueType *Typ)
{
    if (Typ == NULL)
        return 0;
    if (Typ->FPFormat == FPStorageBinary32)
    {
        struct FloatAlign { char x; float y; } a;
        return (int)((char *)&a.y - &a.x);
    }
    return Typ->AlignBytes;
}

static int TypeStorageAlign(const struct ValueType *Typ)
{
    if (Typ == NULL)
        return 0;
    return Typ->Base == TypeFP ? TypeFPStorageAlign(Typ) : Typ->AlignBytes;
}

/* memory used by a variable given its type and array size */
int TypeSize(struct ValueType* Typ, int ArraySize, int Compact)
{
    if (IS_INTEGER_NUMERIC_TYPE(Typ) && !Compact)
    {
        int BaseSize = Typ->Sizeof;
        int AlignSize = sizeof(ALIGN_TYPE);
        return (BaseSize > AlignSize) ? MEM_ALIGN(BaseSize) : AlignSize;
    }
    else if (Typ->Base != TypeArray)
        return Typ->Base == TypeFP ? TypeFPStorageSize(Typ) : Typ->Sizeof;
    else
    {
        struct ValueType *ElemType = Typ->FromType;
        int ElemSize = ElemType->Base == TypeFP ? TypeFPStorageSize(ElemType) : ElemType->Sizeof;
        return ElemSize * ArraySize;
    }
}

/* does a type (through arrays and nested structs) contain a union?  Unions
 * overlap their members, so per-byte init tracking would flag legal type
 * punning; such objects are treated permissively. */
int TypeContainsUnion(struct ValueType *Typ)
{
    int Guard = 0;
    while (Typ != NULL && Typ->Base == TypeArray && Guard++ < 16)
        Typ = Typ->FromType;
    if (Typ == NULL)
        return 0;
    if (Typ->Base == TypeUnion)
        return 1;
    if (Typ->Base == TypeStruct && Typ->Members != NULL)
    {
        int i;
        for (i = 0; i < Typ->Members->Size; i++)
        {
            struct TableEntry *E;
            for (E = Typ->Members->HashTable[i]; E != NULL; E = E->Next)
                if (E->p.v.Val != NULL && E->p.v.Val->Typ != Typ
                    && TypeContainsUnion(E->p.v.Val->Typ))
                    return 1;
        }
    }
    return 0;
}

/* max bytes of an automatic array/struct that get a per-byte init bitmap */
#define INIT_TRACK_MAX_BYTES 65536

/* can an automatic object of this type carry a per-byte init bitmap? */
int TypeInitTrackable(struct ValueType *Typ)
{
    int Bytes;
    if (Typ == NULL)
        return 0;
    if (Typ->Base == TypeArray)
    {
        if (Typ->ArraySize <= 0 || Typ->FromType == NULL)
            return 0;
        Bytes = (Typ->FromType->Base == TypeFP ? TypeFPStorageSize(Typ->FromType) : Typ->FromType->Sizeof) * Typ->ArraySize;
    }
    else if (Typ->Base == TypeStruct)
        Bytes = Typ->Sizeof;
    else
        return 0;
    if (Bytes <= 0 || Bytes > INIT_TRACK_MAX_BYTES)
        return 0;
    return !TypeContainsUnion(Typ);
}

/* add a base type */
void TypeAddBaseType(Picoc *pc, struct ValueType *TypeNode, enum BaseType Base, int Sizeof, int AlignBytes)
{
    TypeNode->Base = Base;
    TypeNode->ArraySize = 0;
    TypeNode->Sizeof = Sizeof;
    TypeNode->AlignBytes = AlignBytes;
    TypeNode->Identifier = pc->StrEmpty;
    TypeNode->Members = NULL;
    TypeNode->FromType = NULL;
    TypeNode->DerivedTypeList = NULL;
    TypeNode->OnHeap = FALSE;
    TypeNode->Next = pc->UberType.DerivedTypeList;
    pc->UberType.DerivedTypeList = TypeNode;
}

/* initialise the type system */
void TypeInit(Picoc *pc)
{
    struct IntAlign { char x; int y; } ia;
    struct ShortAlign { char x; short y; } sa;
    struct CharAlign { char x; char y; } ca;
    struct LongAlign { char x; long y; } la;
    struct LongLongAlign { char x; long long y; } lla;
    int LongLongAlignBytes = (char*)&lla.y - &lla.x;
#ifndef NO_FP
    struct DoubleAlign { char x; double y; } da;
#endif
    struct PointerAlign { char x; void *y; } pa;
    
    IntAlignBytes = (char *)&ia.y - &ia.x;
    PointerAlignBytes = (char *)&pa.y - &pa.x;
    
    pc->UberType.DerivedTypeList = NULL;
    TypeAddBaseType(pc, &pc->IntType, TypeInt, sizeof(int), IntAlignBytes);
    TypeAddBaseType(pc, &pc->ShortType, TypeShort, sizeof(short), (char *)&sa.y - &sa.x);
    TypeAddBaseType(pc, &pc->CharType, TypeChar, sizeof(char), (char *)&ca.y - &ca.x);
    TypeAddBaseType(pc, &pc->LongType, TypeLong, sizeof(long), (char *)&la.y - &la.x);
    TypeAddBaseType(pc, &pc->UnsignedIntType, TypeUnsignedInt, sizeof(unsigned int), IntAlignBytes);
    TypeAddBaseType(pc, &pc->UnsignedShortType, TypeUnsignedShort, sizeof(unsigned short), (char *)&sa.y - &sa.x);
    TypeAddBaseType(pc, &pc->UnsignedLongType, TypeUnsignedLong, sizeof(unsigned long), (char *)&la.y - &la.x);
    TypeAddBaseType(pc, &pc->UnsignedLongLongType, TypeUnsignedLongLong, sizeof(unsigned long long), LongLongAlignBytes);
    TypeAddBaseType(pc, &pc->LongLongType, TypeLongLong, sizeof(long long), LongLongAlignBytes);
    TypeAddBaseType(pc, &pc->UnsignedCharType, TypeUnsignedChar, sizeof(unsigned char), (char *)&ca.y - &ca.x);
    TypeAddBaseType(pc, &pc->VoidType, TypeVoid, 0, 1);
    TypeAddBaseType(pc, &pc->FunctionType, TypeFunction, sizeof(int), IntAlignBytes);
    TypeAddBaseType(pc, &pc->MacroType, TypeMacro, sizeof(int), IntAlignBytes);
    TypeAddBaseType(pc, &pc->GotoLabelType, TypeGotoLabel, 0, 1);
#ifndef NO_FP
    struct { char x; float y; } fa;
    TypeAddBaseType(pc, &pc->FPType, TypeFP, sizeof(double), (char *)&da.y - &da.x);
    pc->FPType.FPFormat = FPStorageBinary64;
    /* Keep the interpreter's scalar working slot double-sized. Object layout
     * and memory accesses use FPFormat via TypeFPStorageSize/Align instead. */
    TypeAddBaseType(pc, &pc->FloatType, TypeFP, sizeof(float), (char *)&fa.y - &fa.x);
    pc->FloatType.FPFormat = FPStorageBinary32;
    TypeAddBaseType(pc, &pc->TypeType, Type_Type, sizeof(double), (char *)&da.y - &da.x);  /* must be large enough to cast to a double */
#else
    TypeAddBaseType(pc, &pc->TypeType, Type_Type, sizeof(struct ValueType *), PointerAlignBytes);
#endif
    pc->CharArrayType = TypeAdd(pc, NULL, &pc->CharType, TypeArray, 0, pc->StrEmpty, sizeof(char), (char *)&ca.y - &ca.x);
    pc->CharPtrType = TypeAdd(pc, NULL, &pc->CharType, TypePointer, 0, pc->StrEmpty, sizeof(void *), PointerAlignBytes);
    pc->CharPtrPtrType = TypeAdd(pc, NULL, pc->CharPtrType, TypePointer, 0, pc->StrEmpty, sizeof(void *), PointerAlignBytes);
    pc->VoidPtrType = TypeAdd(pc, NULL, &pc->VoidType, TypePointer, 0, pc->StrEmpty, sizeof(void *), PointerAlignBytes);
}

/* deallocate heap-allocated types */
void TypeCleanupNode(Picoc *pc, struct ValueType *Typ)
{
    struct ValueType *SubType;
    struct ValueType *NextSubType;
    
    /* clean up and free all the sub-nodes */
    for (SubType = Typ->DerivedTypeList; SubType != NULL; SubType = NextSubType)
    {
        NextSubType = SubType->Next;
        TypeCleanupNode(pc, SubType);
        if (SubType->OnHeap)
        {
            /* if it's a struct or union deallocate all the member values */
            if (SubType->Members != NULL)
            {
                VariableTableCleanup(pc, SubType->Members);
                HeapFreeMem(pc, SubType->Members);
            }

            /* free this node */
            HeapFreeMem(pc, SubType);
        }
    }
}

void TypeCleanup(Picoc *pc)
{
    TypeCleanupNode(pc, &pc->UberType);
}

/* parse a struct or union declaration */
void TypeParseStruct(struct ParseState *Parser, struct ValueType **Typ, int IsStruct)
{
    struct Value *LexValue;
    struct ValueType *MemberType;
    char *MemberIdentifier;
    char *StructIdentifier;
    struct Value *MemberValue;
    enum LexToken Token;
    int AlignBoundary;
    Picoc *pc = Parser->pc;
    
    Token = LexGetToken(Parser, &LexValue, FALSE);
    if (Token == TokenIdentifier)
    {
        LexGetToken(Parser, &LexValue, TRUE);
        StructIdentifier = LexValue->Val->Identifier;
        Token = LexGetToken(Parser, NULL, FALSE);
    }
    else
    {
        static char TempNameBuf[7] = "^s0000";
        StructIdentifier = PlatformMakeTempName(pc, TempNameBuf);
    }

    *Typ = TypeGetMatching(pc, Parser, &Parser->pc->UberType, IsStruct ? TypeStruct : TypeUnion, 0, StructIdentifier, TRUE);
    Token = LexGetToken(Parser, NULL, FALSE);
    if (Token == TokenLeftBrace && (*Typ)->Members != NULL)
    {
        /* New body for same tag: fresh type so earlier locals keep old Members (113). */
        *Typ = TypeAdd(pc, Parser, &pc->UberType,
                       IsStruct ? TypeStruct : TypeUnion, 0,
                       (*Typ)->Identifier, 0, 0);
    }

    if (Token != TokenLeftBrace)
    { 
        /* use the already defined structure */
        return;
    }
    
    /* Struct/union definitions allowed in any scope (block or global). */
    LexGetToken(Parser, NULL, TRUE);    
    (*Typ)->Members = VariableAlloc(pc, Parser, sizeof(struct Table) + STRUCT_TABLE_SIZE * sizeof(struct TableEntry), TRUE);
    (*Typ)->Members->HashTable = (struct TableEntry **)((char *)(*Typ)->Members + sizeof(struct Table));
    TableInitTable((*Typ)->Members, (struct TableEntry **)((char *)(*Typ)->Members + sizeof(struct Table)), STRUCT_TABLE_SIZE, TRUE);
    
    /* Bitfield packing state (struct only; unions treat each bitfield as a
     * full unit at offset 0 for v1).  GCC-like: pack consecutive bitfields of
     * the same declared base type into storage units of that type's size,
     * LSB-first within the unit. */
    int BF_UnitOffset = 0;      /* byte offset of current storage unit */
    int BF_BitsUsed = 0;        /* bits already taken in current unit */
    int BF_UnitBytes = 0;       /* size of current unit in bytes */
    struct ValueType *BF_UnitType = NULL;
    int DeclCounter = 0;          /* declaration order for init */

    do {
        struct ValueType *BasicMemberType = NULL;
        int IsBitField = 0;
        int BitWidth = 0;

        TypeParse(Parser, &MemberType, &MemberIdentifier, NULL);
        if (MemberType == NULL)
            ProgramFail(Parser, "invalid type in struct");
        /* MemberIdentifier may be StrEmpty for anonymous bitfields: int : 3; */
        if (MemberIdentifier == NULL)
            MemberIdentifier = pc->StrEmpty;
        BasicMemberType = MemberType;

        /* optional bitfield width:  type [ident] : width */
        if (LexGetToken(Parser, NULL, FALSE) == TokenColon)
        {
            LexGetToken(Parser, NULL, TRUE); /* consume ':' */
            BitWidth = (int)ExpressionParseInt(Parser);
            IsBitField = 1;
            if (BitWidth < 0)
                ProgramFail(Parser, "negative bitfield width");
            if (BitWidth > 0 && BitWidth > MemberType->Sizeof * 8)
                ProgramFail(Parser, "bitfield width %d exceeds type size", BitWidth);
            /* zero-width only allowed as anonymous padding */
            if (BitWidth == 0 && MemberIdentifier != pc->StrEmpty)
                ProgramFail(Parser, "zero-width bitfield must be anonymous");
        }

        for (;;)
        {
            MemberValue = VariableAllocValueAndData(pc, Parser, sizeof(int), FALSE, NULL, TRUE);
            MemberValue->Typ = MemberType;
            MemberValue->BitOffset = 0;
            MemberValue->BitWidth = 0;
            MemberValue->DeclIndex = (unsigned short)DeclCounter++;

            if (IsBitField)
            {
                int UnitBytes = MemberType->Sizeof;
                int UnitBits = UnitBytes * 8;

                if (UnitBytes <= 0)
                    ProgramFail(Parser, "invalid type for bitfield");

                if (IsStruct)
                {
                    if (BitWidth == 0)
                    {
                        /* zero-width: force alignment to next unit boundary */
                        if (BF_BitsUsed > 0)
                        {
                            (*Typ)->Sizeof = BF_UnitOffset + BF_UnitBytes;
                            BF_BitsUsed = 0;
                            BF_UnitType = NULL;
                        }
                        AlignBoundary = TypeStorageAlign(MemberType) > 0 ? TypeStorageAlign(MemberType) : 1;
                        if (((*Typ)->Sizeof & (AlignBoundary - 1)) != 0)
                            (*Typ)->Sizeof += AlignBoundary - ((*Typ)->Sizeof & (AlignBoundary - 1));
                        MemberValue->Val->Integer = (*Typ)->Sizeof;
                        MemberValue->BitWidth = 0;
                        /* do not TableSet anonymous padding */
                    }
                    else
                    {
                        /* New unit if not enough bits, or storage unit size changes.
                         * Signed vs unsigned of the SAME size share one unit (GCC/MSVC:
                         * int a:6; unsigned b:6 packs into one int). Comparing
                         * Type* identity alone forced sizeof(S)=8 for that pattern
                         * and broke pointer walks over BF arrays on some hosts. */
                        {
                            int same_unit = (BF_UnitType != NULL
                                && BF_UnitType->Sizeof == MemberType->Sizeof
                                && IS_INTEGER_NUMERIC_TYPE(BF_UnitType)
                                && IS_INTEGER_NUMERIC_TYPE(MemberType)
                                && BF_BitsUsed > 0
                                && BF_BitsUsed + BitWidth <= UnitBits);
                            if (!same_unit)
                            {
                                if (BF_BitsUsed > 0)
                                    (*Typ)->Sizeof = BF_UnitOffset + BF_UnitBytes;
                                AlignBoundary = TypeStorageAlign(MemberType) > 0 ? TypeStorageAlign(MemberType) : 1;
                                if (((*Typ)->Sizeof & (AlignBoundary - 1)) != 0)
                                    (*Typ)->Sizeof += AlignBoundary - ((*Typ)->Sizeof & (AlignBoundary - 1));
                                BF_UnitOffset = (*Typ)->Sizeof;
                                BF_BitsUsed = 0;
                                BF_UnitBytes = UnitBytes;
                                BF_UnitType = MemberType;
                            }
                        }
                        MemberValue->Val->Integer = BF_UnitOffset;
                        MemberValue->BitOffset = (unsigned char)BF_BitsUsed;
                        MemberValue->BitWidth = (unsigned char)BitWidth;
                        BF_BitsUsed += BitWidth;
                        if (BF_UnitOffset + BF_UnitBytes > (*Typ)->Sizeof)
                            (*Typ)->Sizeof = BF_UnitOffset + BF_UnitBytes;
                    }
                }
                else
                {
                    /* union: each bitfield sits at offset 0 in its own unit */
                    MemberValue->Val->Integer = 0;
                    MemberValue->BitOffset = 0;
                    MemberValue->BitWidth = (unsigned char)BitWidth;
                    if (UnitBytes > (*Typ)->Sizeof)
                        (*Typ)->Sizeof = UnitBytes;
                }

                if ((*Typ)->AlignBytes < (TypeStorageAlign(MemberType)))
                    (*Typ)->AlignBytes = TypeStorageAlign(MemberType);

                /* named bitfield: publish in member table */
                if (MemberIdentifier != pc->StrEmpty)
                {
                    if (!TableSet(pc, (*Typ)->Members, MemberIdentifier, MemberValue,
                            Parser->FileName, Parser->Line, Parser->CharacterPos))
                        ProgramFail(Parser, "member '%s' already defined", MemberIdentifier);
                }
                else
                {
                    /* Anonymous bitfields affect layout only; they are not
                     * reachable from the member table, so release metadata. */
                    VariableFree(pc, MemberValue);
                }
                /* anonymous: layout advanced, descriptor not in table */
            }
            else
            {
                /* normal (non-bitfield) member: flush any open bitfield unit */
                if (IsStruct && BF_BitsUsed > 0)
                {
                    (*Typ)->Sizeof = BF_UnitOffset + BF_UnitBytes;
                    BF_BitsUsed = 0;
                    BF_UnitType = NULL;
                }

                if (IsStruct)
                {
                    AlignBoundary = TypeStorageAlign(MemberValue->Typ);
                    if (AlignBoundary > 0 && ((*Typ)->Sizeof & (AlignBoundary - 1)) != 0)
                        (*Typ)->Sizeof += AlignBoundary - ((*Typ)->Sizeof & (AlignBoundary - 1));
                    MemberValue->Val->Integer = (*Typ)->Sizeof;
                    (*Typ)->Sizeof += TypeSizeValue(MemberValue, TRUE);
                }
                else
                {
                    MemberValue->Val->Integer = 0;
                    if (TypeSizeValue(MemberValue, TRUE) > (*Typ)->Sizeof)
                        (*Typ)->Sizeof = TypeSizeValue(MemberValue, TRUE);
                }

                if ((*Typ)->AlignBytes < (TypeStorageAlign(MemberValue->Typ)))
                    (*Typ)->AlignBytes = TypeStorageAlign(MemberValue->Typ);

                if (MemberIdentifier == pc->StrEmpty)
                    ProgramFail(Parser, "identifier expected for struct member");
                if (!TableSet(pc, (*Typ)->Members, MemberIdentifier, MemberValue,
                        Parser->FileName, Parser->Line, Parser->CharacterPos))
                    ProgramFail(Parser, "member '%s' already defined", MemberIdentifier);
            }

            /* allow: int a, b, c;  or  int a:3, b:5; */
            if (LexGetToken(Parser, NULL, FALSE) == TokenComma)
            {
                LexGetToken(Parser, NULL, TRUE);
                TypeParseIdentPart(Parser, BasicMemberType, &MemberType, &MemberIdentifier);
                if (MemberIdentifier == NULL)
                    MemberIdentifier = pc->StrEmpty;
                /* width may be re-specified after comma: a:3, b:5 */
                if (LexGetToken(Parser, NULL, FALSE) == TokenColon)
                {
                    LexGetToken(Parser, NULL, TRUE);
                    BitWidth = (int)ExpressionParseInt(Parser);
                    IsBitField = 1;
                    if (BitWidth < 0)
                        ProgramFail(Parser, "negative bitfield width");
                    if (BitWidth > 0 && BitWidth > MemberType->Sizeof * 8)
                        ProgramFail(Parser, "bitfield width %d exceeds type size", BitWidth);
                    if (BitWidth == 0 && MemberIdentifier != pc->StrEmpty)
                        ProgramFail(Parser, "zero-width bitfield must be anonymous");
                }
                else if (!IsBitField && (MemberIdentifier == NULL || MemberIdentifier == pc->StrEmpty))
                    ProgramFail(Parser, "identifier expected after ',' in struct");
                continue;
            }
            break;
        }

        if (LexGetToken(Parser, NULL, TRUE) != TokenSemicolon)
            ProgramFail(Parser, "semicolon expected");

    } while (LexGetToken(Parser, NULL, FALSE) != TokenRightBrace);

    /* flush trailing bitfield unit */
    if (IsStruct && BF_BitsUsed > 0)
        (*Typ)->Sizeof = BF_UnitOffset + BF_UnitBytes;

    /* now align the structure to the size of its largest member's alignment */
    AlignBoundary = (*Typ)->AlignBytes;
    if (AlignBoundary > 0 && ((*Typ)->Sizeof & (AlignBoundary - 1)) != 0)
        (*Typ)->Sizeof += AlignBoundary - ((*Typ)->Sizeof & (AlignBoundary - 1));

    LexGetToken(Parser, NULL, TRUE);
}

/* create a system struct which has no user-visible members */
struct ValueType *TypeCreateOpaqueStruct(Picoc *pc, struct ParseState *Parser, const char *StructName, int Size)
{
    struct ValueType *Typ = TypeGetMatching(pc, Parser, &pc->UberType, TypeStruct, 0, StructName, FALSE);
    
    /* create the (empty) table */
    Typ->Members = VariableAlloc(pc, Parser, sizeof(struct Table) + STRUCT_TABLE_SIZE * sizeof(struct TableEntry), TRUE);
    Typ->Members->HashTable = (struct TableEntry **)((char *)Typ->Members + sizeof(struct Table));
    TableInitTable(Typ->Members, (struct TableEntry **)((char *)Typ->Members + sizeof(struct Table)), STRUCT_TABLE_SIZE, TRUE);
    Typ->Sizeof = Size;
    
    return Typ;
}

/* parse an enum declaration */
void TypeParseEnum(struct ParseState *Parser, struct ValueType **Typ)
{
    struct Value *LexValue;
    struct Value InitValue;
    enum LexToken Token;
    PTR_TYPE_INT EnumValue = 0;
    char *EnumIdentifier;
    Picoc *pc = Parser->pc;
    
    Token = LexGetToken(Parser, &LexValue, FALSE);
    if (Token == TokenIdentifier)
    {
        LexGetToken(Parser, &LexValue, TRUE);
        EnumIdentifier = LexValue->Val->Identifier;
        Token = LexGetToken(Parser, NULL, FALSE);
    }
    else
    {
        static char TempNameBuf[7] = "^e0000";
        EnumIdentifier = PlatformMakeTempName(pc, TempNameBuf);
    }

    TypeGetMatching(pc, Parser, &pc->UberType, TypeEnum, 0, EnumIdentifier, Token != TokenLeftBrace);
    *Typ = &pc->IntType;
    if (Token != TokenLeftBrace)
    { 
        /* use the already defined enum */
        if ((*Typ)->Members == NULL)
            ProgramFail(Parser, "enum '%s' isn't defined", EnumIdentifier);
            
        return;
    }
    
    /* Enum definitions allowed in block scope as well as global. */
    LexGetToken(Parser, NULL, TRUE);    
    (*Typ)->Members = &pc->GlobalTable;
    memset((void *)&InitValue, '\0', sizeof(struct Value));
    InitValue.Typ = &pc->IntType;
    InitValue.Val = (union AnyValue *)&EnumValue;
    do {
        if (LexGetToken(Parser, &LexValue, TRUE) != TokenIdentifier)
            ProgramFail(Parser, "identifier expected");
        
        EnumIdentifier = LexValue->Val->Identifier;
        if (LexGetToken(Parser, NULL, FALSE) == TokenAssign)
        {
            LexGetToken(Parser, NULL, TRUE);
            EnumValue = ExpressionParseInt(Parser);
        }
        
        /* MakeWritable=FALSE → IsLValue=0: true rvalue constant (not a cell).
         * Mark IsConst so assign-to-enum-name and folds share one rule. */
        {
            struct Value *EV = VariableDefine(pc, Parser, EnumIdentifier,
                                             &InitValue, NULL, FALSE);
            if (EV != NULL)
                EV->IsConst = TRUE;
        }
            
        Token = LexGetToken(Parser, NULL, TRUE);
        if (Token != TokenComma && Token != TokenRightBrace)
            ProgramFail(Parser, "comma expected");
        
        EnumValue++;

        /* C99 trailing comma: enum { A = 1, } */
        if (Token == TokenComma
            && LexGetToken(Parser, NULL, FALSE) == TokenRightBrace)
        {
            LexGetToken(Parser, NULL, TRUE); /* absorb } */
            break;
        }
                    
    } while (Token == TokenComma);
}

/* parse a type - just the basic type */
int TypeParseFront(struct ParseState *Parser, struct ValueType **Typ, int *IsStatic)
{
    struct ParseState Before;
    struct Value *LexerValue;
    enum LexToken Token;
    int Unsigned = FALSE;
    struct Value *VarValue;
    int StaticQualifier = FALSE;
    Picoc *pc = Parser->pc;
    *Typ = NULL;

    /* ignore leading type qualifiers */
    ParserCopy(&Before, Parser);
    Token = LexGetToken(Parser, &LexerValue, TRUE);
    /* reset declaration qualifiers for this type parse */
    Parser->DeclIsConst = FALSE;
    Parser->DeclIsVolatile = FALSE;

    while (Token == TokenStaticType || Token == TokenAutoType || Token == TokenRegisterType || Token == TokenExternType
           || Token == TokenConstType || Token == TokenVolatileType)
    {
        if (Token == TokenStaticType)
            StaticQualifier = TRUE;
        if (Token == TokenConstType)
            Parser->DeclIsConst = TRUE;
        if (Token == TokenVolatileType)
            Parser->DeclIsVolatile = TRUE;
            
        Token = LexGetToken(Parser, &LexerValue, TRUE);
    }
    
    if (IsStatic != NULL)
        *IsStatic = StaticQualifier;
        
    /* handle signed/unsigned with no trailing type */
    if (Token == TokenSignedType || Token == TokenUnsignedType)
    {
        enum LexToken FollowToken = LexGetToken(Parser, &LexerValue, FALSE);
        Unsigned = (Token == TokenUnsignedType);
        
        if (FollowToken != TokenIntType && FollowToken != TokenLongType && FollowToken != TokenShortType && FollowToken != TokenCharType)
        {
            if (Token == TokenUnsignedType)
                *Typ = &pc->UnsignedIntType;
            else
                *Typ = &pc->IntType;
            
            return TRUE;
        }
        
        Token = LexGetToken(Parser, &LexerValue, TRUE);
    }
    
    switch (Token)
    {
        case TokenIntType: *Typ = Unsigned ? &pc->UnsignedIntType : &pc->IntType; break;
        case TokenShortType: *Typ = Unsigned ? &pc->UnsignedShortType : &pc->ShortType; break;
        case TokenCharType: *Typ = Unsigned ? &pc->UnsignedCharType : &pc->CharType; break;
        case TokenLongType:
            /* Check for long long */
            if (LexGetToken(Parser, NULL, FALSE) == TokenLongType) {
                LexGetToken(Parser, NULL, TRUE);   /* consume the second 'long' */
                *Typ = Unsigned ? &Parser->pc->UnsignedLongLongType : &Parser->pc->LongLongType;
            }
            else {
                *Typ = Unsigned ? &Parser->pc->UnsignedLongType : &Parser->pc->LongType;
            }
            break;
#ifndef NO_FP
        case TokenFloatType: case TokenDoubleType: *Typ = &pc->FPType; break;
#endif
        case TokenVoidType: *Typ = &pc->VoidType; break;
        
        case TokenStructType: case TokenUnionType: 
            if (*Typ != NULL)
                ProgramFail(Parser, "bad type declaration");
                
            TypeParseStruct(Parser, Typ, Token == TokenStructType);
            break;

        case TokenEnumType:
            if (*Typ != NULL)
                ProgramFail(Parser, "bad type declaration");
                
            TypeParseEnum(Parser, Typ);
            break;
        
        case TokenIdentifier:
            /* we already know it's a typedef-defined type because we got here */
            VariableGet(pc, Parser, LexerValue->Val->Identifier, &VarValue);
            *Typ = VarValue->Val->Typ;
            break;

        default: ParserCopy(Parser, &Before); return FALSE;
    }
    
    return TRUE;
}

/* parse a type - the part at the end after the identifier. eg. array specifications etc. */
struct ValueType *TypeParseBack(struct ParseState *Parser, struct ValueType *FromType)
{
    enum LexToken Token;
    struct ParseState Before;

    ParserCopy(&Before, Parser);
    Token = LexGetToken(Parser, NULL, TRUE);
    if (Token == TokenLeftSquareBracket)
    {
        /* add another array bound */
        if (LexGetToken(Parser, NULL, FALSE) == TokenRightSquareBracket)
        {
            /* an unsized array */
            LexGetToken(Parser, NULL, TRUE);
            return TypeGetMatching(Parser->pc, Parser, TypeParseBack(Parser, FromType), TypeArray, 0, Parser->pc->StrEmpty, TRUE);
        }
        else
        {
            /* get a numeric array size */
            enum RunMode OldMode = Parser->Mode;
            ptrdiff_t ArraySize;
            Parser->Mode = RunModeRun;
            ArraySize = (ptrdiff_t)ExpressionParseInt(Parser);
            Parser->Mode = OldMode;
            
            if (LexGetToken(Parser, NULL, TRUE) != TokenRightSquareBracket)
                ProgramFail(Parser, "']' expected");
            
            return TypeGetMatching(Parser->pc, Parser, TypeParseBack(Parser, FromType), TypeArray, ArraySize, Parser->pc->StrEmpty, TRUE);
        }
    }
    else
    {
        /* the type specification has finished */
        ParserCopy(Parser, &Before);
        return FromType;
    }
}

/* parse a type - the part which is repeated with each identifier in a declaration list */
void TypeParseIdentPart(struct ParseState *Parser, struct ValueType *BasicTyp, struct ValueType **Typ, char **Identifier)
{
    struct ParseState Before;
    enum LexToken Token;
    struct Value *LexValue;
    int Done = FALSE;
    *Typ = BasicTyp;
    *Identifier = Parser->pc->StrEmpty;
    
    while (!Done)
    {
        ParserCopy(&Before, Parser);
        Token = LexGetToken(Parser, &LexValue, TRUE);
        switch (Token)
        {
            case TokenOpenBracket:
                /* Function pointer: int (*name)(params)  or  nested abstract type */
                if (LexGetToken(Parser, NULL, FALSE) == TokenAsterisk)
                {
                    struct ValueType *ReturnType = *Typ;
                    struct ValueType *FuncType;
                    enum LexToken Tok2;
                    struct Value *LexVal = NULL;
                    int ParamCount = 0;
                    struct ValueType *ParamTypes[16];

                    if (ReturnType == NULL)
                        ProgramFail(Parser, "bad type declaration");

                    LexGetToken(Parser, NULL, TRUE); /* consume '*' */

                    /* C abstract declarator allows no name: int (*)(void *, void *)
                     * in parameter lists / prototypes. Named form int (*cmp)(...) still
                     * requires the identifier when present. */
                    Tok2 = LexGetToken(Parser, &LexVal, FALSE);
                    if (Tok2 == TokenIdentifier)
                    {
                        LexGetToken(Parser, &LexVal, TRUE);
                        *Identifier = LexVal->Val->Identifier;
                    }
                    else if (Tok2 == TokenCloseBracket
                             || Tok2 == TokenLeftSquareBracket)
                    {
                        *Identifier = Parser->pc->StrEmpty;
                    }
                    else
                        ProgramFail(Parser, "identifier expected after '(*'");

                    /* Optional array: int (*tab[N])(params) → array of fptr */
                    {
                        int ArraySize = 0;
                        int IsArrayOfFptr = 0;
                        Tok2 = LexGetToken(Parser, NULL, FALSE);
                        if (Tok2 == TokenLeftSquareBracket)
                        {
                            struct Value *SzVal = NULL;
                            LexGetToken(Parser, NULL, TRUE); /* '[' */
                            Tok2 = LexGetToken(Parser, &SzVal, TRUE);
                            if (Tok2 != TokenIntegerConstant && Tok2 != TokenCharacterConstant)
                                ProgramFail(Parser, "array size expected in function pointer array");
                            ArraySize = (int)SzVal->Val->Integer;
                            if (ArraySize < 0)
                                ProgramFail(Parser, "negative array size");
                            if (LexGetToken(Parser, NULL, TRUE) != TokenRightSquareBracket)
                                ProgramFail(Parser, "']' expected");
                            IsArrayOfFptr = 1;
                        }

                    if (LexGetToken(Parser, NULL, TRUE) != TokenCloseBracket)
                        ProgramFail(Parser, "')' expected after function pointer name");

                    /* Pointer to a fixed-size array: int (*a)[N]. This is not
                     * a function pointer; parse the suffix after ')' as the
                     * pointee array type, then wrap that array in a pointer. */
                    if (LexGetToken(Parser, NULL, FALSE) == TokenLeftSquareBracket)
                    {
                        struct ValueType *ArrayType = TypeParseBack(Parser, ReturnType);
                        *Typ = TypeGetMatching(Parser->pc, Parser, ArrayType,
                                               TypePointer, 0, Parser->pc->StrEmpty, TRUE);
                        Done = TRUE;
                        break;
                    }

                    /* Optional parameter list. A terminal ellipsis is part of
                     * the declarator grammar, not a parameter type: accept it
                     * after the fixed parameters for both named and abstract
                     * function-pointer declarators, including typedefs. */
                    if (LexGetToken(Parser, NULL, FALSE) == TokenOpenBracket)
                    {
                        LexGetToken(Parser, NULL, TRUE); /* '(' */
                        if (LexGetToken(Parser, NULL, FALSE) != TokenCloseBracket)
                        {
                            for (;;)
                            {
                                struct ValueType *PType = NULL;
                                char *PIdent = NULL;
                                if (LexGetToken(Parser, NULL, FALSE) == TokenEllipsis)
                                {
                                    if (ParamCount == 0)
                                        ProgramFail(Parser, "ellipsis requires a fixed parameter in function pointer");
                                    LexGetToken(Parser, NULL, TRUE); /* '...' */
                                    if (LexGetToken(Parser, NULL, TRUE) != TokenCloseBracket)
                                        ProgramFail(Parser, "ellipsis must be last in function pointer parameters");
                                    break;
                                }
                                TypeParse(Parser, &PType, &PIdent, NULL);
                                if (PType == NULL)
                                    ProgramFail(Parser, "parameter type expected");
                                if (ParamCount < 16)
                                    ParamTypes[ParamCount++] = PType;
                                Tok2 = LexGetToken(Parser, NULL, TRUE);
                                if (Tok2 == TokenComma)
                                {
                                    if (LexGetToken(Parser, NULL, FALSE) == TokenEllipsis)
                                        continue;
                                    continue;
                                }
                                if (Tok2 == TokenCloseBracket)
                                    break;
                                ProgramFail(Parser, "',' or ')' expected in function pointer");
                            }
                        }
                        else
                            LexGetToken(Parser, NULL, TRUE); /* ')' empty params */
                    }

                    /* Build function type → pointer-to-function; optionally array. */
                    (void)ParamCount;
                    (void)ParamTypes;
                    FuncType = TypeGetMatching(Parser->pc, Parser, ReturnType,
                                               TypeFunction, 0, Parser->pc->StrEmpty, TRUE);
                    *Typ = TypeGetMatching(Parser->pc, Parser, FuncType,
                                           TypePointer, 0, Parser->pc->StrEmpty, TRUE);
                    if (IsArrayOfFptr)
                        *Typ = TypeGetMatching(Parser->pc, Parser, *Typ,
                                               TypeArray, ArraySize, Parser->pc->StrEmpty, TRUE);
                    Done = TRUE;
                    break;
                    } /* array-of-fptr scope */
                }

                if (*Typ != NULL)
                    ProgramFail(Parser, "bad type declaration");
                
                TypeParse(Parser, Typ, Identifier, NULL);
                if (LexGetToken(Parser, NULL, TRUE) != TokenCloseBracket)
                    ProgramFail(Parser, "')' expected");
                break;
                
            case TokenAsterisk:
                if (*Typ == NULL)
                    ProgramFail(Parser, "bad type declaration");

                /* C: const in "const T *p" qualifies T, not p.
                 * So a leading DeclIsConst must not make the pointer variable read-only.
                 * "T * const p" will set DeclIsConst again via TokenConstType after '*'. */
                Parser->DeclIsConst = FALSE;

                *Typ = TypeGetMatching(Parser->pc, Parser, *Typ, TypePointer, 0, Parser->pc->StrEmpty, TRUE);
                break;

            /* const/volatile after the base type or after '*'
             * (e.g. "char const *", "const char * const", "volatile int") */
            case TokenConstType:
                Parser->DeclIsConst = TRUE;
                break;
            case TokenVolatileType:
                Parser->DeclIsVolatile = TRUE;
                break;
            
            case TokenIdentifier:
                if (*Typ == NULL || *Identifier != Parser->pc->StrEmpty)
                    ProgramFail(Parser, "bad type declaration");
                
                *Identifier = LexValue->Val->Identifier;
                Done = TRUE;
                break;
                
            default: ParserCopy(Parser, &Before); Done = TRUE; break;
        }
    }
    
    if (*Typ == NULL)
        ProgramFail(Parser, "bad type declaration");

    if (*Identifier != Parser->pc->StrEmpty)
    { 
        /* parse stuff after the identifier */
        *Typ = TypeParseBack(Parser, *Typ);
    }
}

/* parse a type - a complete declaration including identifier */
void TypeParse(struct ParseState *Parser, struct ValueType **Typ, char **Identifier, int *IsStatic)
{
    struct ValueType *BasicType;
    
    TypeParseFront(Parser, &BasicType, IsStatic);
    TypeParseIdentPart(Parser, BasicType, Typ, Identifier);
}

/* check if a type has been fully defined - otherwise it's just a forward declaration */
int TypeIsForwardDeclared(struct ParseState *Parser, struct ValueType *Typ)
{
    if (Typ->Base == TypeArray)
        return TypeIsForwardDeclared(Parser, Typ->FromType);
    
    if ( (Typ->Base == TypeStruct || Typ->Base == TypeUnion) && Typ->Members == NULL)
        return TRUE;
        
    return FALSE;
}
