//===----- CGCall.h - Encapsulate calling convention details ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// These classes wrap the information about a call or function
// definition used to handle ABI compliancy.
//
//===----------------------------------------------------------------------===//

#include "CGCall.h"
#include "CodeGenFunction.h"
#include "CodeGenModule.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/RecordLayout.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Attributes.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Target/TargetData.h"
using namespace clang;
using namespace CodeGen;

static llvm::cl::opt<bool>
UseX86_64ABI("use-x86_64-abi",
           llvm::cl::desc("Enable use of experimental x86_64 ABI."),
           llvm::cl::init(false));

/***/

// FIXME: Use iterator and sidestep silly type array creation.

CGFunctionInfo::CGFunctionInfo(const FunctionTypeNoProto *FTNP)
  : IsVariadic(true)
{
  ArgTypes.push_back(FTNP->getResultType());
}

CGFunctionInfo::CGFunctionInfo(const FunctionTypeProto *FTP)
  : IsVariadic(FTP->isVariadic())
{
  ArgTypes.push_back(FTP->getResultType());
  for (unsigned i = 0, e = FTP->getNumArgs(); i != e; ++i)
    ArgTypes.push_back(FTP->getArgType(i));
}

// FIXME: Is there really any reason to have this still?
CGFunctionInfo::CGFunctionInfo(const FunctionDecl *FD)
{
  const FunctionType *FTy = FD->getType()->getAsFunctionType();
  const FunctionTypeProto *FTP = dyn_cast<FunctionTypeProto>(FTy);

  ArgTypes.push_back(FTy->getResultType());
  if (FTP) {
    IsVariadic = FTP->isVariadic();
    for (unsigned i = 0, e = FTP->getNumArgs(); i != e; ++i)
      ArgTypes.push_back(FTP->getArgType(i));
  } else {
    IsVariadic = true;
  }
}

CGFunctionInfo::CGFunctionInfo(const ObjCMethodDecl *MD,
                               const ASTContext &Context)
  : IsVariadic(MD->isVariadic())
{
  ArgTypes.push_back(MD->getResultType());
  ArgTypes.push_back(MD->getSelfDecl()->getType());
  ArgTypes.push_back(Context.getObjCSelType());
  for (ObjCMethodDecl::param_const_iterator i = MD->param_begin(),
         e = MD->param_end(); i != e; ++i)
    ArgTypes.push_back((*i)->getType());
}

ArgTypeIterator CGFunctionInfo::argtypes_begin() const {
  return ArgTypes.begin();
}

ArgTypeIterator CGFunctionInfo::argtypes_end() const {
  return ArgTypes.end();
}

/***/

CGCallInfo::CGCallInfo(QualType _ResultType, const CallArgList &_Args) {
  ArgTypes.push_back(_ResultType);
  for (CallArgList::const_iterator i = _Args.begin(), e = _Args.end(); i!=e; ++i)
    ArgTypes.push_back(i->second);
}

ArgTypeIterator CGCallInfo::argtypes_begin() const {
  return ArgTypes.begin();
}

ArgTypeIterator CGCallInfo::argtypes_end() const {
  return ArgTypes.end();
}

/***/

/// ABIArgInfo - Helper class to encapsulate information about how a
/// specific C type should be passed to or returned from a function.
class ABIArgInfo {
public:
  enum Kind {
    Default,
    StructRet, /// Only valid for return values. The return value
               /// should be passed through a pointer to a caller
               /// allocated location passed as an implicit first
               /// argument to the function.

    Ignore,    /// Ignore the argument (treat as void). Useful for
               /// void and empty structs.

    Coerce,    /// Only valid for aggregate return types, the argument
               /// should be accessed by coercion to a provided type.

    ByVal,     /// Only valid for aggregate argument types. The
               /// structure should be passed "byval" with the
               /// specified alignment (0 indicates default
               /// alignment).

    Expand,    /// Only valid for aggregate argument types. The
               /// structure should be expanded into consecutive
               /// arguments for its constituent fields. Currently
               /// expand is only allowed on structures whose fields
               /// are all scalar types or are themselves expandable
               /// types.

    KindFirst=Default, KindLast=Expand
  };

private:
  Kind TheKind;
  const llvm::Type *TypeData;
  unsigned UIntData;

  ABIArgInfo(Kind K, const llvm::Type *TD=0,
             unsigned UI=0) : TheKind(K),
                              TypeData(TD),
                              UIntData(0) {}
public:
  static ABIArgInfo getDefault() { 
    return ABIArgInfo(Default); 
  }
  static ABIArgInfo getStructRet() { 
    return ABIArgInfo(StructRet); 
  }
  static ABIArgInfo getIgnore() {
    return ABIArgInfo(Ignore);
  }
  static ABIArgInfo getCoerce(const llvm::Type *T) { 
    return ABIArgInfo(Coerce, T);
  }
  static ABIArgInfo getByVal(unsigned Alignment) {
    return ABIArgInfo(ByVal, 0, Alignment);
  }
  static ABIArgInfo getExpand() {
    return ABIArgInfo(Expand);
  }

  Kind getKind() const { return TheKind; }
  bool isDefault() const { return TheKind == Default; }
  bool isStructRet() const { return TheKind == StructRet; }
  bool isIgnore() const { return TheKind == Ignore; }
  bool isCoerce() const { return TheKind == Coerce; }
  bool isByVal() const { return TheKind == ByVal; }
  bool isExpand() const { return TheKind == Expand; }

  // Coerce accessors
  const llvm::Type *getCoerceToType() const {
    assert(TheKind == Coerce && "Invalid kind!");
    return TypeData;
  }

  // ByVal accessors
  unsigned getByValAlignment() const {
    assert(TheKind == ByVal && "Invalid kind!");
    return UIntData;
  }
};

/***/

/* FIXME: All of this stuff should be part of the target interface
   somehow. It is currently here because it is not clear how to factor
   the targets to support this, since the Targets currently live in a
   layer below types n'stuff.
 */

/// ABIInfo - Target specific hooks for defining how a type should be
/// passed or returned from functions.
class clang::ABIInfo {
public:
  virtual ~ABIInfo();

  virtual ABIArgInfo classifyReturnType(QualType RetTy, 
                                        ASTContext &Context) const = 0;

  virtual ABIArgInfo classifyArgumentType(QualType Ty,
                                          ASTContext &Context) const = 0;
};

ABIInfo::~ABIInfo() {}

/// isEmptyStruct - Return true iff a structure has no non-empty
/// members. Note that a structure with a flexible array member is not
/// considered empty.
static bool isEmptyStruct(QualType T) {
  const RecordType *RT = T->getAsStructureType();
  if (!RT)
    return 0;
  const RecordDecl *RD = RT->getDecl();
  if (RD->hasFlexibleArrayMember())
    return false;
  for (RecordDecl::field_iterator i = RD->field_begin(), 
         e = RD->field_end(); i != e; ++i) {
    const FieldDecl *FD = *i;
    if (!isEmptyStruct(FD->getType()))
      return false;
  }
  return true;
}

/// isSingleElementStruct - Determine if a structure is a "single
/// element struct", i.e. it has exactly one non-empty field or
/// exactly one field which is itself a single element
/// struct. Structures with flexible array members are never
/// considered single element structs.
///
/// \return The field declaration for the single non-empty field, if
/// it exists.
static const FieldDecl *isSingleElementStruct(QualType T) {
  const RecordType *RT = T->getAsStructureType();
  if (!RT)
    return 0;

  const RecordDecl *RD = RT->getDecl();
  if (RD->hasFlexibleArrayMember())
    return 0;

  const FieldDecl *Found = 0;
  for (RecordDecl::field_iterator i = RD->field_begin(), 
         e = RD->field_end(); i != e; ++i) {
    const FieldDecl *FD = *i;
    QualType FT = FD->getType();

    if (isEmptyStruct(FT)) {
      // Ignore
    } else if (Found) {
      return 0;
    } else if (!CodeGenFunction::hasAggregateLLVMType(FT)) {
      Found = FD;
    } else {
      Found = isSingleElementStruct(FT);
      if (!Found)
        return 0;
    }
  }

  return Found;
}

static bool is32Or64BitBasicType(QualType Ty, ASTContext &Context) {
  if (!Ty->getAsBuiltinType() && !Ty->isPointerType())
    return false;

  uint64_t Size = Context.getTypeSize(Ty);
  return Size == 32 || Size == 64;
}

static bool areAllFields32Or64BitBasicType(const RecordDecl *RD,
                                           ASTContext &Context) {
  for (RecordDecl::field_iterator i = RD->field_begin(), 
         e = RD->field_end(); i != e; ++i) {
    const FieldDecl *FD = *i;

    if (!is32Or64BitBasicType(FD->getType(), Context))
      return false;
    
    // If this is a bit-field we need to make sure it is still a
    // 32-bit or 64-bit type.
    if (Expr *BW = FD->getBitWidth()) {
      unsigned Width = BW->getIntegerConstantExprValue(Context).getZExtValue();
      if (Width <= 16)
        return false;
    }
  }
  return true;
}

namespace {
/// DefaultABIInfo - The default implementation for ABI specific
/// details. This implementation provides information which results in
/// sensible LLVM IR generation, but does not conform to any
/// particular ABI.
class DefaultABIInfo : public ABIInfo {
  virtual ABIArgInfo classifyReturnType(QualType RetTy, 
                                        ASTContext &Context) const;

  virtual ABIArgInfo classifyArgumentType(QualType RetTy,
                                          ASTContext &Context) const;
};

/// X86_32ABIInfo - The X86-32 ABI information.
class X86_32ABIInfo : public ABIInfo {
public:
  virtual ABIArgInfo classifyReturnType(QualType RetTy, 
                                        ASTContext &Context) const;

  virtual ABIArgInfo classifyArgumentType(QualType RetTy,
                                          ASTContext &Context) const;
};
}

ABIArgInfo X86_32ABIInfo::classifyReturnType(QualType RetTy,
                                            ASTContext &Context) const {
  if (CodeGenFunction::hasAggregateLLVMType(RetTy)) {
    // Classify "single element" structs as their element type.
    const FieldDecl *SeltFD = isSingleElementStruct(RetTy);
    if (SeltFD) {
      QualType SeltTy = SeltFD->getType()->getDesugaredType();
      if (const BuiltinType *BT = SeltTy->getAsBuiltinType()) {
        // FIXME: This is gross, it would be nice if we could just
        // pass back SeltTy and have clients deal with it. Is it worth
        // supporting coerce to both LLVM and clang Types?
        if (BT->isIntegerType()) {
          uint64_t Size = Context.getTypeSize(SeltTy);
          return ABIArgInfo::getCoerce(llvm::IntegerType::get((unsigned) Size));
        } else if (BT->getKind() == BuiltinType::Float) {
          return ABIArgInfo::getCoerce(llvm::Type::FloatTy);
        } else if (BT->getKind() == BuiltinType::Double) {
          return ABIArgInfo::getCoerce(llvm::Type::DoubleTy);
        }
      } else if (SeltTy->isPointerType()) {
        // FIXME: It would be really nice if this could come out as
        // the proper pointer type.
        llvm::Type *PtrTy = 
          llvm::PointerType::getUnqual(llvm::Type::Int8Ty);
        return ABIArgInfo::getCoerce(PtrTy);
      }
    }

    uint64_t Size = Context.getTypeSize(RetTy);
    if (Size == 8) {
      return ABIArgInfo::getCoerce(llvm::Type::Int8Ty);
    } else if (Size == 16) {
      return ABIArgInfo::getCoerce(llvm::Type::Int16Ty);
    } else if (Size == 32) {
      return ABIArgInfo::getCoerce(llvm::Type::Int32Ty);
    } else if (Size == 64) {
      return ABIArgInfo::getCoerce(llvm::Type::Int64Ty);
    } else {
      return ABIArgInfo::getStructRet();
    }
  } else {
    return ABIArgInfo::getDefault();
  }
}

ABIArgInfo X86_32ABIInfo::classifyArgumentType(QualType Ty,
                                              ASTContext &Context) const {
  if (CodeGenFunction::hasAggregateLLVMType(Ty)) {
    // Structures with flexible arrays are always byval.
    if (const RecordType *RT = Ty->getAsStructureType())
      if (RT->getDecl()->hasFlexibleArrayMember())
        return ABIArgInfo::getByVal(0);

    // Expand empty structs (i.e. ignore)
    uint64_t Size = Context.getTypeSize(Ty);
    if (Ty->isStructureType() && Size == 0)
      return ABIArgInfo::getExpand();

    // Expand structs with size <= 128-bits which consist only of
    // basic types (int, long long, float, double, xxx*). This is
    // non-recursive and does not ignore empty fields.
    if (const RecordType *RT = Ty->getAsStructureType()) {
      if (Context.getTypeSize(Ty) <= 4*32 &&
          areAllFields32Or64BitBasicType(RT->getDecl(), Context))
        return ABIArgInfo::getExpand();
    }

    return ABIArgInfo::getByVal(0);
  } else {
    return ABIArgInfo::getDefault();
  }
}

namespace {
/// X86_32ABIInfo - The X86_64 ABI information.
class X86_64ABIInfo : public ABIInfo {
  enum Class {
    Integer = 0,
    SSE,
    SSEUp,
    X87,
    X87Up,
    ComplexX87,
    NoClass,
    Memory
  };

  /// classify - Determine the x86_64 register classes in which the
  /// given type T should be passed.
  ///
  /// \param Lo - The classification for the low word of the type.
  /// \param Hi - The classification for the high word of the type.
  /// \param OffsetBase - The byte position of the type in the root
  /// structure. Some parameters are classified different depending on
  /// whether they straddle an eightbyte boundary.
  ///
  /// If a word is unused its result will be NoClass; if a type should
  /// be passed in Memory then at least the classification of \arg Lo
  /// will be Memory.
  ///
  /// The \arg Lo class will be NoClass iff the argument is ignored.
  ///
  /// If the \arg Lo class is ComplexX87, then the \arg Hi class will
  /// be NoClass.
  void classify(QualType T, ASTContext &Context, uint64_t OffsetBase,
                Class &Lo, Class &Hi) const;

public:
  virtual ABIArgInfo classifyReturnType(QualType RetTy, 
                                        ASTContext &Context) const;

  virtual ABIArgInfo classifyArgumentType(QualType RetTy,
                                          ASTContext &Context) const;
};
}

void X86_64ABIInfo::classify(QualType Ty,
                             ASTContext &Context,
                             uint64_t OffsetBase,
                             Class &Lo, Class &Hi) const {
  Lo = Memory;
  Hi = NoClass;
  if (const BuiltinType *BT = Ty->getAsBuiltinType()) {
    BuiltinType::Kind k = BT->getKind();

    if (k == BuiltinType::Void) {
      Lo = NoClass; 
    } else if (k >= BuiltinType::Bool && k <= BuiltinType::LongLong) {
      Lo = Integer;
    } else if (k == BuiltinType::Float || k == BuiltinType::Double) {
      Lo = SSE;
    } else if (k == BuiltinType::LongDouble) {
      Lo = X87;
      Hi = X87Up;
    }
    
    // FIXME: _Decimal32 and _Decimal64 are SSE.
    // FIXME: _float128 and _Decimal128 are (SSE, SSEUp).
    // FIXME: __int128 is (Integer, Integer).
  } else if (Ty->isPointerLikeType() || Ty->isBlockPointerType() ||
             Ty->isObjCQualifiedInterfaceType()) {
    Lo = Integer;
  } else if (const VectorType *VT = Ty->getAsVectorType()) {
    uint64_t Size = Context.getTypeSize(VT);
    if (Size == 64) {
      // FIXME: For some reason, gcc appears to be treating <1 x
      // double> as INTEGER; this seems wrong, but we will match for
      // now (icc rejects <1 x double>, so...).
      Lo = (VT->getElementType() == Context.DoubleTy) ? Integer : SSE;
    } else if (Size == 128) {
      Lo = SSE;
      Hi = SSEUp;
    }
  } else if (const ComplexType *CT = Ty->getAsComplexType()) {
    QualType ET = CT->getElementType();
    
    if (ET->isIntegerType()) {
      uint64_t Size = Context.getTypeSize(Ty);
      if (Size <= 64)
        Lo = Integer;
      else if (Size <= 128)
        Lo = Hi = Integer;
    } else if (ET == Context.FloatTy) 
      Lo = SSE;
    else if (ET == Context.DoubleTy)
      Lo = Hi = SSE;
    else if (ET == Context.LongDoubleTy)
      Lo = ComplexX87;

    // If this complex type crosses an eightbyte boundary then it
    // should be split.
    uint64_t EB_Real = (OffsetBase) >> 3;
    uint64_t EB_Imag = (OffsetBase + Context.getTypeSize(ET)) >> 3;
    if (Hi == NoClass && EB_Real != EB_Imag)
      Hi = Lo;
  } else if (const RecordType *RT = Ty->getAsRecordType()) {
    uint64_t Size = Context.getTypeSize(Ty);
    
    // AMD64-ABI 3.2.3p2: Rule 1. If the size of an object is larger
    // than two eightbytes, ..., it has class MEMORY.
    if (Size > 128)
      return;

    const RecordDecl *RD = RT->getDecl();

    // Assume variable sized types are passed in memory.
    if (RD->hasFlexibleArrayMember())
      return;

    const ASTRecordLayout &Layout = Context.getASTRecordLayout(RD);
    
    // Reset Lo class, this will be recomputed.
    Lo = NoClass;
    unsigned idx = 0;
    for (RecordDecl::field_iterator i = RD->field_begin(), 
           e = RD->field_end(); i != e; ++i, ++idx) {
      unsigned Offset = OffsetBase + Layout.getFieldOffset(idx);

      //  AMD64-ABI 3.2.3p2: Rule 1. If ..., or it contains unaligned
      //  fields, it has class MEMORY.
      if (Offset % Context.getTypeAlign(i->getType())) {
        Lo = Memory;
        return;
      }

      // Determine which half of the structure we are classifying.
      //
      // AMD64-ABI 3.2.3p2: Rule 3. f the size of the aggregate
      // exceeds a single eightbyte, each is classified
      // separately. Each eightbyte gets initialized to class
      // NO_CLASS.
      Class &Target = Offset < 64 ? Lo : Hi;

      // Classify this field.
      Class FieldLo, FieldHi;
      classify(i->getType(), Context, Offset, FieldLo, FieldHi);
      
      // Merge the lo field classifcation.
      //
      // AMD64-ABI 3.2.3p2: Rule 4. Each field of an object is
      // classified recursively so that always two fields are
      // considered. The resulting class is calculated according to
      // the classes of the fields in the eightbyte:
      //
      // (a) If both classes are equal, this is the resulting class.
      //
      // (b) If one of the classes is NO_CLASS, the resulting class is
      // the other class.
      //
      // (c) If one of the classes is MEMORY, the result is the MEMORY
      // class.
      //
      // (d) If one of the classes is INTEGER, the result is the
      // INTEGER.
      //
      // (e) If one of the classes is X87, X87UP, COMPLEX_X87 class,
      // MEMORY is used as class.
      //
      // (f) Otherwise class SSE is used.
      if (Target == FieldLo || FieldLo == NoClass) ;
      else if (FieldLo == Memory) {
        // Memory is never over-ridden, just bail.
        Lo = Memory;
        return;
      } 
      else if (Target == NoClass)
        Target = FieldLo;
      else if (Target == Integer || FieldLo == Integer) 
        Target = Integer;
      else if (FieldLo == X87 || FieldLo == X87Up || FieldLo == ComplexX87) {
        // As before, just bail once we generate a memory class.
        Lo = Memory;
        return;
      } else
        Target = SSE;

      // It isn't clear from the ABI spec what the role of the high
      // classification is here, but since this should only happen
      // when we have a struct with a two eightbyte member, we can
      // just push the field high class into the overall high class.
      if (FieldHi != NoClass)
        Hi = FieldHi;
    }

    // AMD64-ABI 3.2.3p2: Rule 5. Then a post merger cleanup is done:
    //
    // (a) If one of the classes is MEMORY, the whole argument is
    // passed in memory.
    //
    // (b) If SSEUP is not preceeded by SSE, it is converted to SSE.

    // The first of these conditions is guaranteed by how we implement
    // the merge (just bail). I don't believe the second is actually
    // possible at all.
    assert(Lo != Memory && "Unexpected memory classification.");
    if (Hi == SSEUp && Lo != SSE)
        Hi = SSE;
  }
}

ABIArgInfo X86_64ABIInfo::classifyReturnType(QualType RetTy,
                                            ASTContext &Context) const {
  // AMD64-ABI 3.2.3p4: Rule 1. Classify the return type with the
  // classification algorithm.
  X86_64ABIInfo::Class Lo, Hi;
  classify(RetTy, Context, 0, Lo, Hi);

  const llvm::Type *ResType = 0;
  switch (Lo) {
  case NoClass:
    return ABIArgInfo::getIgnore();

  case SSEUp:
  case X87Up:
    assert(0 && "Invalid classification for lo word.");

  // AMD64-ABI 3.2.3p4: Rule 2. Types of class memory are returned via
  // hidden argument, i.e. structret.
  case Memory:
    return ABIArgInfo::getStructRet();

    // AMD64-ABI 3.2.3p4: Rule 3. If the class is INTEGER, the next
    // available register of the sequence %rax, %rdx is used.
  case Integer:
    ResType = llvm::Type::Int64Ty; break;

    // AMD64-ABI 3.2.3p4: Rule 4. If the class is SSE, the next
    // available SSE register of the sequence %xmm0, %xmm1 is used.
  case SSE:
    ResType = llvm::Type::DoubleTy; break;

    // AMD64-ABI 3.2.3p4: Rule 6. If the class is X87, the value is
    // returned on the X87 stack in %st0 as 80-bit x87 number.
  case X87:
    ResType = llvm::Type::X86_FP80Ty; break;

  // AMD64-ABI 3.2.3p4: Rule 8. If the class is COMPLEX_X87, the real
  // part of the value is returned in %st0 and the imaginary part in
  // %st1.
  case ComplexX87:
    assert(Hi == NoClass && "Unexpected ComplexX87 classification.");
    ResType = llvm::VectorType::get(llvm::Type::X86_FP80Ty, 2);
    break;    
  }

  switch (Hi) {
    // Memory was handled previously, and ComplexX87 and X87 should
    // never occur as hi classes.
  case Memory:
  case X87:
  case ComplexX87:
    assert(0 && "Invalid classification for hi word.");

  case NoClass: break;
  case Integer:
    ResType = llvm::StructType::get(ResType, llvm::Type::Int64Ty, NULL);
    break;
  case SSE:    
    ResType = llvm::StructType::get(ResType, llvm::Type::DoubleTy, NULL);
    break;

    // AMD64-ABI 3.2.3p4: Rule 5. If the class is SSEUP, the eightbyte
    // is passed in the upper half of the last used SSE register.
    //
    // SSEUP should always be preceeded by SSE, just widen.
  case SSEUp:
    assert(Lo == SSE && "Unexpected SSEUp classification.");
    ResType = llvm::VectorType::get(llvm::Type::DoubleTy, 2);
    break;

    // AMD64-ABI 3.2.3p4: Rule 7. If the class is X87UP, the value is
    // returned together with the previous X87 value in %st0.
    //
    // X87UP should always be preceeded by X87, so we don't need to do
    // anything here.
  case X87Up:
    assert(Lo == X87 && "Unexpected X87Up classification.");
    break;
  }

  return ABIArgInfo::getCoerce(ResType);
}

ABIArgInfo X86_64ABIInfo::classifyArgumentType(QualType Ty,
                                              ASTContext &Context) const {
  return ABIArgInfo::getDefault();
}

ABIArgInfo DefaultABIInfo::classifyReturnType(QualType RetTy,
                                            ASTContext &Context) const {
  return ABIArgInfo::getDefault();
}

ABIArgInfo DefaultABIInfo::classifyArgumentType(QualType Ty,
                                              ASTContext &Context) const {
  return ABIArgInfo::getDefault();
}

const ABIInfo &CodeGenTypes::getABIInfo() const {
  if (TheABIInfo)
    return *TheABIInfo;

  // For now we just cache this in the CodeGenTypes and don't bother
  // to free it.
  const char *TargetPrefix = getContext().Target.getTargetPrefix();
  if (strcmp(TargetPrefix, "x86") == 0) {
    switch (getContext().Target.getPointerWidth(0)) {
    case 32:
      return *(TheABIInfo = new X86_32ABIInfo());
    case 64:
      if (UseX86_64ABI)
        return *(TheABIInfo = new X86_64ABIInfo());
    }
  }

  return *(TheABIInfo = new DefaultABIInfo);
}

// getABIReturnInfo - Wrap the ABIInfo getABIReturnInfo, altering
// "default" types to StructRet when appropriate for simplicity.
static ABIArgInfo getABIReturnInfo(QualType Ty, CodeGenTypes &CGT) {
  assert(!Ty->isArrayType() && 
         "Array types cannot be passed directly.");
  ABIArgInfo Info = CGT.getABIInfo().classifyReturnType(Ty, CGT.getContext());
  // Ensure default on aggregate types is StructRet.
  if (Info.isDefault() && CodeGenFunction::hasAggregateLLVMType(Ty))
    return ABIArgInfo::getStructRet();
  return Info;
}

// getABIArgumentInfo - Wrap the ABIInfo getABIReturnInfo, altering
// "default" types to ByVal when appropriate for simplicity.
static ABIArgInfo getABIArgumentInfo(QualType Ty, CodeGenTypes &CGT) {
  assert(!Ty->isArrayType() && 
         "Array types cannot be passed directly.");
  ABIArgInfo Info = CGT.getABIInfo().classifyArgumentType(Ty, CGT.getContext());
  // Ensure default on aggregate types is ByVal.
  if (Info.isDefault() && CodeGenFunction::hasAggregateLLVMType(Ty))
    return ABIArgInfo::getByVal(0);
  return Info;  
}

/***/

void CodeGenTypes::GetExpandedTypes(QualType Ty, 
                                    std::vector<const llvm::Type*> &ArgTys) {
  const RecordType *RT = Ty->getAsStructureType();
  assert(RT && "Can only expand structure types.");
  const RecordDecl *RD = RT->getDecl();
  assert(!RD->hasFlexibleArrayMember() && 
         "Cannot expand structure with flexible array.");
  
  for (RecordDecl::field_iterator i = RD->field_begin(), 
         e = RD->field_end(); i != e; ++i) {
    const FieldDecl *FD = *i;
    assert(!FD->isBitField() && 
           "Cannot expand structure with bit-field members.");
    
    QualType FT = FD->getType();
    if (CodeGenFunction::hasAggregateLLVMType(FT)) {
      GetExpandedTypes(FT, ArgTys);
    } else {
      ArgTys.push_back(ConvertType(FT));
    }
  }
}

llvm::Function::arg_iterator 
CodeGenFunction::ExpandTypeFromArgs(QualType Ty, LValue LV,
                                    llvm::Function::arg_iterator AI) {
  const RecordType *RT = Ty->getAsStructureType();
  assert(RT && "Can only expand structure types.");

  RecordDecl *RD = RT->getDecl();
  assert(LV.isSimple() && 
         "Unexpected non-simple lvalue during struct expansion.");  
  llvm::Value *Addr = LV.getAddress();
  for (RecordDecl::field_iterator i = RD->field_begin(), 
         e = RD->field_end(); i != e; ++i) {
    FieldDecl *FD = *i;    
    QualType FT = FD->getType();

    // FIXME: What are the right qualifiers here?
    LValue LV = EmitLValueForField(Addr, FD, false, 0);
    if (CodeGenFunction::hasAggregateLLVMType(FT)) {
      AI = ExpandTypeFromArgs(FT, LV, AI);
    } else {
      EmitStoreThroughLValue(RValue::get(AI), LV, FT);
      ++AI;
    }
  }

  return AI;
}

void 
CodeGenFunction::ExpandTypeToArgs(QualType Ty, RValue RV, 
                                  llvm::SmallVector<llvm::Value*, 16> &Args) {
  const RecordType *RT = Ty->getAsStructureType();
  assert(RT && "Can only expand structure types.");

  RecordDecl *RD = RT->getDecl();
  assert(RV.isAggregate() && "Unexpected rvalue during struct expansion");
  llvm::Value *Addr = RV.getAggregateAddr();
  for (RecordDecl::field_iterator i = RD->field_begin(), 
         e = RD->field_end(); i != e; ++i) {
    FieldDecl *FD = *i;    
    QualType FT = FD->getType();
    
    // FIXME: What are the right qualifiers here?
    LValue LV = EmitLValueForField(Addr, FD, false, 0);
    if (CodeGenFunction::hasAggregateLLVMType(FT)) {
      ExpandTypeToArgs(FT, RValue::getAggregate(LV.getAddress()), Args);
    } else {
      RValue RV = EmitLoadOfLValue(LV, FT);
      assert(RV.isScalar() && 
             "Unexpected non-scalar rvalue during struct expansion.");
      Args.push_back(RV.getScalarVal());
    }
  }
}

/***/

const llvm::FunctionType *
CodeGenTypes::GetFunctionType(const CGCallInfo &CI, bool IsVariadic) {
  return GetFunctionType(CI.argtypes_begin(), CI.argtypes_end(), IsVariadic);
}

const llvm::FunctionType *
CodeGenTypes::GetFunctionType(const CGFunctionInfo &FI) {
  return GetFunctionType(FI.argtypes_begin(), FI.argtypes_end(), FI.isVariadic());
}

const llvm::FunctionType *
CodeGenTypes::GetFunctionType(ArgTypeIterator begin, ArgTypeIterator end,
                              bool IsVariadic) {
  std::vector<const llvm::Type*> ArgTys;

  const llvm::Type *ResultType = 0;

  QualType RetTy = *begin;
  ABIArgInfo RetAI = getABIReturnInfo(RetTy, *this);
  switch (RetAI.getKind()) {
  case ABIArgInfo::ByVal:
  case ABIArgInfo::Expand:
    assert(0 && "Invalid ABI kind for return argument");

  case ABIArgInfo::Default:
    if (RetTy->isVoidType()) {
      ResultType = llvm::Type::VoidTy;
    } else {
      ResultType = ConvertType(RetTy);
    }
    break;

  case ABIArgInfo::StructRet: {
    ResultType = llvm::Type::VoidTy;
    const llvm::Type *STy = ConvertType(RetTy);
    ArgTys.push_back(llvm::PointerType::get(STy, RetTy.getAddressSpace()));
    break;
  }

  case ABIArgInfo::Ignore:
    ResultType = llvm::Type::VoidTy;
    break;

  case ABIArgInfo::Coerce:
    ResultType = RetAI.getCoerceToType();
    break;
  }
  
  for (++begin; begin != end; ++begin) {
    ABIArgInfo AI = getABIArgumentInfo(*begin, *this);
    const llvm::Type *Ty = ConvertType(*begin);
    
    switch (AI.getKind()) {
    case ABIArgInfo::Ignore:
      break;

    case ABIArgInfo::Coerce:
    case ABIArgInfo::StructRet:
      assert(0 && "Invalid ABI kind for non-return argument");
    
    case ABIArgInfo::ByVal:
      // byval arguments are always on the stack, which is addr space #0.
      ArgTys.push_back(llvm::PointerType::getUnqual(Ty));
      assert(AI.getByValAlignment() == 0 && "FIXME: alignment unhandled");
      break;
      
    case ABIArgInfo::Default:
      ArgTys.push_back(Ty);
      break;
     
    case ABIArgInfo::Expand:
      GetExpandedTypes(*begin, ArgTys);
      break;
    }
  }

  return llvm::FunctionType::get(ResultType, ArgTys, IsVariadic);
}

bool CodeGenModule::ReturnTypeUsesSret(QualType RetTy) {
  return getABIReturnInfo(RetTy, getTypes()).isStructRet();
}

void CodeGenModule::ConstructAttributeList(const Decl *TargetDecl,
                                           ArgTypeIterator begin,
                                           ArgTypeIterator end,
                                           AttributeListType &PAL) {
  unsigned FuncAttrs = 0;
  unsigned RetAttrs = 0;

  if (TargetDecl) {
    if (TargetDecl->getAttr<NoThrowAttr>())
      FuncAttrs |= llvm::Attribute::NoUnwind;
    if (TargetDecl->getAttr<NoReturnAttr>())
      FuncAttrs |= llvm::Attribute::NoReturn;
    if (TargetDecl->getAttr<PureAttr>())
      FuncAttrs |= llvm::Attribute::ReadOnly;
    if (TargetDecl->getAttr<ConstAttr>())
      FuncAttrs |= llvm::Attribute::ReadNone;
  }

  QualType RetTy = *begin;
  unsigned Index = 1;
  ABIArgInfo RetAI = getABIReturnInfo(RetTy, getTypes());
  switch (RetAI.getKind()) {
  case ABIArgInfo::Default:
    if (RetTy->isPromotableIntegerType()) {
      if (RetTy->isSignedIntegerType()) {
        RetAttrs |= llvm::Attribute::SExt;
      } else if (RetTy->isUnsignedIntegerType()) {
        RetAttrs |= llvm::Attribute::ZExt;
      }
    }
    break;

  case ABIArgInfo::StructRet:
    PAL.push_back(llvm::AttributeWithIndex::get(Index, 
                                                  llvm::Attribute::StructRet|
                                                  llvm::Attribute::NoAlias));
    ++Index;
    break;

  case ABIArgInfo::Ignore:
  case ABIArgInfo::Coerce:
    break;

  case ABIArgInfo::ByVal:
  case ABIArgInfo::Expand:
    assert(0 && "Invalid ABI kind for return argument");    
  }

  if (RetAttrs)
    PAL.push_back(llvm::AttributeWithIndex::get(0, RetAttrs));
  for (++begin; begin != end; ++begin) {
    QualType ParamType = *begin;
    unsigned Attributes = 0;
    ABIArgInfo AI = getABIArgumentInfo(ParamType, getTypes());
    
    switch (AI.getKind()) {
    case ABIArgInfo::StructRet:
    case ABIArgInfo::Coerce:
      assert(0 && "Invalid ABI kind for non-return argument");
    
    case ABIArgInfo::ByVal:
      Attributes |= llvm::Attribute::ByVal;
      assert(AI.getByValAlignment() == 0 && "FIXME: alignment unhandled");
      break;
      
    case ABIArgInfo::Default:
      if (ParamType->isPromotableIntegerType()) {
        if (ParamType->isSignedIntegerType()) {
          Attributes |= llvm::Attribute::SExt;
        } else if (ParamType->isUnsignedIntegerType()) {
          Attributes |= llvm::Attribute::ZExt;
        }
      }
      break;
     
    case ABIArgInfo::Ignore:
      // Skip increment, no matching LLVM parameter.
      continue; 

    case ABIArgInfo::Expand: {
      std::vector<const llvm::Type*> Tys;  
      // FIXME: This is rather inefficient. Do we ever actually need
      // to do anything here? The result should be just reconstructed
      // on the other side, so extension should be a non-issue.
      getTypes().GetExpandedTypes(ParamType, Tys);
      Index += Tys.size();
      continue;
    }
    }
      
    if (Attributes)
      PAL.push_back(llvm::AttributeWithIndex::get(Index, Attributes));
    ++Index;
  }
  if (FuncAttrs)
    PAL.push_back(llvm::AttributeWithIndex::get(~0, FuncAttrs));

}

void CodeGenFunction::EmitFunctionProlog(llvm::Function *Fn,
                                         QualType RetTy, 
                                         const FunctionArgList &Args) {
  // Emit allocs for param decls.  Give the LLVM Argument nodes names.
  llvm::Function::arg_iterator AI = Fn->arg_begin();
  
  // Name the struct return argument.
  if (CGM.ReturnTypeUsesSret(RetTy)) {
    AI->setName("agg.result");
    ++AI;
  }
     
  for (FunctionArgList::const_iterator i = Args.begin(), e = Args.end();
       i != e; ++i) {
    const VarDecl *Arg = i->first;
    QualType Ty = i->second;
    ABIArgInfo ArgI = getABIArgumentInfo(Ty, CGM.getTypes());

    switch (ArgI.getKind()) {
    case ABIArgInfo::ByVal: 
    case ABIArgInfo::Default: {
      assert(AI != Fn->arg_end() && "Argument mismatch!");
      llvm::Value* V = AI;
      if (!getContext().typesAreCompatible(Ty, Arg->getType())) {
        // This must be a promotion, for something like
        // "void a(x) short x; {..."
        V = EmitScalarConversion(V, Ty, Arg->getType());
      }
      EmitParmDecl(*Arg, V);
      break;
    }
      
    case ABIArgInfo::Expand: {
      // If this was structure was expand into multiple arguments then
      // we need to create a temporary and reconstruct it from the
      // arguments.
      std::string Name = Arg->getNameAsString();
      llvm::Value *Temp = CreateTempAlloca(ConvertType(Ty), 
                                           (Name + ".addr").c_str());
      // FIXME: What are the right qualifiers here?
      llvm::Function::arg_iterator End = 
        ExpandTypeFromArgs(Ty, LValue::MakeAddr(Temp,0), AI);      
      EmitParmDecl(*Arg, Temp);

      // Name the arguments used in expansion and increment AI.
      unsigned Index = 0;
      for (; AI != End; ++AI, ++Index)
        AI->setName(Name + "." + llvm::utostr(Index));
      continue;
    }

    case ABIArgInfo::Ignore:
      break;

    case ABIArgInfo::Coerce:
    case ABIArgInfo::StructRet:
      assert(0 && "Invalid ABI kind for non-return argument");        
    }

    ++AI;
  }
  assert(AI == Fn->arg_end() && "Argument mismatch!");
}

/// CreateCoercedLoad - Create a load from \arg SrcPtr interpreted as
/// a pointer to an object of type \arg Ty.
///
/// This safely handles the case when the src type is smaller than the
/// destination type; in this situation the values of bits which not
/// present in the src are undefined.
static llvm::Value *CreateCoercedLoad(llvm::Value *SrcPtr,
                                      const llvm::Type *Ty,
                                      CodeGenFunction &CGF) {
  const llvm::Type *SrcTy = 
    cast<llvm::PointerType>(SrcPtr->getType())->getElementType();
  uint64_t SrcSize = CGF.CGM.getTargetData().getTypePaddedSize(SrcTy);
  uint64_t DstSize = CGF.CGM.getTargetData().getTypePaddedSize(Ty);

  // If load is legal, just bitcase the src pointer.
  if (SrcSize == DstSize) {
    llvm::Value *Casted =
      CGF.Builder.CreateBitCast(SrcPtr, llvm::PointerType::getUnqual(Ty));
    return CGF.Builder.CreateLoad(Casted);
  } else {
    assert(SrcSize < DstSize && "Coercion is losing source bits!");

    // Otherwise do coercion through memory. This is stupid, but
    // simple.
    llvm::Value *Tmp = CGF.CreateTempAlloca(Ty);
    llvm::Value *Casted = 
      CGF.Builder.CreateBitCast(Tmp, llvm::PointerType::getUnqual(SrcTy));
    CGF.Builder.CreateStore(CGF.Builder.CreateLoad(SrcPtr), Casted);
    return CGF.Builder.CreateLoad(Tmp);
  }
}

/// CreateCoercedStore - Create a store to \arg DstPtr from \arg Src,
/// where the source and destination may have different types.
///
/// This safely handles the case when the src type is larger than the
/// destination type; the upper bits of the src will be lost.
static void CreateCoercedStore(llvm::Value *Src,
                               llvm::Value *DstPtr,
                               CodeGenFunction &CGF) {
  const llvm::Type *SrcTy = Src->getType();
  const llvm::Type *DstTy = 
    cast<llvm::PointerType>(DstPtr->getType())->getElementType();

  uint64_t SrcSize = CGF.CGM.getTargetData().getTypePaddedSize(SrcTy);
  uint64_t DstSize = CGF.CGM.getTargetData().getTypePaddedSize(DstTy);

  // If store is legal, just bitcase the src pointer.
  if (SrcSize == DstSize) {
    llvm::Value *Casted =
      CGF.Builder.CreateBitCast(DstPtr, llvm::PointerType::getUnqual(SrcTy));
    CGF.Builder.CreateStore(Src, Casted);
  } else {
    assert(SrcSize > DstSize && "Coercion is missing bits!");
    
    // Otherwise do coercion through memory. This is stupid, but
    // simple.
    llvm::Value *Tmp = CGF.CreateTempAlloca(SrcTy);
    CGF.Builder.CreateStore(Src, Tmp);
    llvm::Value *Casted = 
      CGF.Builder.CreateBitCast(Tmp, llvm::PointerType::getUnqual(DstTy));
    CGF.Builder.CreateStore(CGF.Builder.CreateLoad(Casted), DstPtr);
  }
}

void CodeGenFunction::EmitFunctionEpilog(QualType RetTy, 
                                         llvm::Value *ReturnValue) {
  llvm::Value *RV = 0;

  // Functions with no result always return void.
  if (ReturnValue) { 
    ABIArgInfo RetAI = getABIReturnInfo(RetTy, CGM.getTypes());
    
    switch (RetAI.getKind()) {
    case ABIArgInfo::StructRet:
      if (RetTy->isAnyComplexType()) {
        // FIXME: Volatile
        ComplexPairTy RT = LoadComplexFromAddr(ReturnValue, false);
        StoreComplexToAddr(RT, CurFn->arg_begin(), false);
      } else if (CodeGenFunction::hasAggregateLLVMType(RetTy)) {
        EmitAggregateCopy(CurFn->arg_begin(), ReturnValue, RetTy);
      } else {
        Builder.CreateStore(Builder.CreateLoad(ReturnValue), 
                            CurFn->arg_begin());
      }
      break;

    case ABIArgInfo::Default:
      RV = Builder.CreateLoad(ReturnValue);
      break;

    case ABIArgInfo::Ignore:
      break;
      
    case ABIArgInfo::Coerce: {
      RV = CreateCoercedLoad(ReturnValue, RetAI.getCoerceToType(), *this);
      break;
    }

    case ABIArgInfo::ByVal:
    case ABIArgInfo::Expand:
      assert(0 && "Invalid ABI kind for return argument");    
    }
  }
  
  if (RV) {
    Builder.CreateRet(RV);
  } else {
    Builder.CreateRetVoid();
  }
}

RValue CodeGenFunction::EmitCall(llvm::Value *Callee, 
                                 QualType RetTy, 
                                 const CallArgList &CallArgs) {
  llvm::SmallVector<llvm::Value*, 16> Args;

  // Handle struct-return functions by passing a pointer to the
  // location that we would like to return into.
  ABIArgInfo RetAI = getABIReturnInfo(RetTy, CGM.getTypes());
  switch (RetAI.getKind()) {
  case ABIArgInfo::StructRet:
    // Create a temporary alloca to hold the result of the call. :(
    Args.push_back(CreateTempAlloca(ConvertType(RetTy)));
    break;
    
  case ABIArgInfo::Default:
  case ABIArgInfo::Ignore:
  case ABIArgInfo::Coerce:
    break;

  case ABIArgInfo::ByVal:
  case ABIArgInfo::Expand:
    assert(0 && "Invalid ABI kind for return argument");    
  }
  
  for (CallArgList::const_iterator I = CallArgs.begin(), E = CallArgs.end(); 
       I != E; ++I) {
    ABIArgInfo ArgInfo = getABIArgumentInfo(I->second, CGM.getTypes());
    RValue RV = I->first;

    switch (ArgInfo.getKind()) {
    case ABIArgInfo::ByVal: // Default is byval
    case ABIArgInfo::Default:      
      if (RV.isScalar()) {
        Args.push_back(RV.getScalarVal());
      } else if (RV.isComplex()) {
        // Make a temporary alloca to pass the argument.
        Args.push_back(CreateTempAlloca(ConvertType(I->second)));
        StoreComplexToAddr(RV.getComplexVal(), Args.back(), false); 
      } else {
        Args.push_back(RV.getAggregateAddr());
      }
      break;
     
    case ABIArgInfo::Ignore:
      break;

    case ABIArgInfo::StructRet:
    case ABIArgInfo::Coerce:
      assert(0 && "Invalid ABI kind for non-return argument");
      break;

    case ABIArgInfo::Expand:
      ExpandTypeToArgs(I->second, RV, Args);
      break;
    }
  }
  
  llvm::CallInst *CI = Builder.CreateCall(Callee,&Args[0],&Args[0]+Args.size());
  CGCallInfo CallInfo(RetTy, CallArgs);

  // FIXME: Provide TargetDecl so nounwind, noreturn, etc, etc get set.
  CodeGen::AttributeListType AttributeList;
  CGM.ConstructAttributeList(0, 
                             CallInfo.argtypes_begin(), CallInfo.argtypes_end(),
                             AttributeList);
  CI->setAttributes(llvm::AttrListPtr::get(AttributeList.begin(), 
                                         AttributeList.size()));  

  if (const llvm::Function *F = dyn_cast<llvm::Function>(Callee))
    CI->setCallingConv(F->getCallingConv());
  if (CI->getType() != llvm::Type::VoidTy)
    CI->setName("call");

  switch (RetAI.getKind()) {
  case ABIArgInfo::StructRet:
    if (RetTy->isAnyComplexType())
      return RValue::getComplex(LoadComplexFromAddr(Args[0], false));
    else if (CodeGenFunction::hasAggregateLLVMType(RetTy))
      return RValue::getAggregate(Args[0]);
    else 
      return RValue::get(Builder.CreateLoad(Args[0]));

  case ABIArgInfo::Default:
    return RValue::get(RetTy->isVoidType() ? 0 : CI);

  case ABIArgInfo::Ignore:
    if (RetTy->isVoidType())
      return RValue::get(0);
    if (CodeGenFunction::hasAggregateLLVMType(RetTy)) {
      llvm::Value *Res =
        llvm::UndefValue::get(llvm::PointerType::getUnqual(ConvertType(RetTy)));
      return RValue::getAggregate(Res);
    }
    return RValue::get(llvm::UndefValue::get(ConvertType(RetTy)));

  case ABIArgInfo::Coerce: {
    llvm::Value *V = CreateTempAlloca(ConvertType(RetTy), "coerce");
    CreateCoercedStore(CI, V, *this);
    if (RetTy->isAnyComplexType())
      return RValue::getComplex(LoadComplexFromAddr(V, false));
    else if (CodeGenFunction::hasAggregateLLVMType(RetTy))
      return RValue::getAggregate(V);
    else
      return RValue::get(Builder.CreateLoad(V));
  }

  case ABIArgInfo::ByVal:
  case ABIArgInfo::Expand:
    assert(0 && "Invalid ABI kind for return argument");    
  }

  assert(0 && "Unhandled ABIArgInfo::Kind");
  return RValue::get(0);
}
