//===--- Expr.cpp - Expression AST Node Implementation --------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file was developed by Chris Lattner and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the Expr class and subclasses.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/Expr.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/Lex/IdentifierTable.h"
// is this bad layering? I (snaroff) don't think so. Want Chris to weigh in.
#include "clang/Parse/DeclSpec.h" 
using namespace clang;

//===----------------------------------------------------------------------===//
// Primary Expressions.
//===----------------------------------------------------------------------===//

StringLiteral::StringLiteral(const char *strData, unsigned byteLength, 
                             bool Wide, QualType t, SourceLocation firstLoc,
                             SourceLocation lastLoc) : 
  Expr(StringLiteralClass, t) {
  // OPTIMIZE: could allocate this appended to the StringLiteral.
  char *AStrData = new char[byteLength];
  memcpy(AStrData, strData, byteLength);
  StrData = AStrData;
  ByteLength = byteLength;
  IsWide = Wide;
  firstTokLoc = firstLoc;
  lastTokLoc = lastLoc;
}

StringLiteral::~StringLiteral() {
  delete[] StrData;
}

bool UnaryOperator::isPostfix(Opcode Op) {
  switch (Op) {
  case PostInc:
  case PostDec:
    return true;
  default:
    return false;
  }
}

/// getOpcodeStr - Turn an Opcode enum value into the punctuation char it
/// corresponds to, e.g. "sizeof" or "[pre]++".
const char *UnaryOperator::getOpcodeStr(Opcode Op) {
  switch (Op) {
  default: assert(0 && "Unknown unary operator");
  case PostInc: return "++";
  case PostDec: return "--";
  case PreInc:  return "++";
  case PreDec:  return "--";
  case AddrOf:  return "&";
  case Deref:   return "*";
  case Plus:    return "+";
  case Minus:   return "-";
  case Not:     return "~";
  case LNot:    return "!";
  case Real:    return "__real";
  case Imag:    return "__imag";
  case SizeOf:  return "sizeof";
  case AlignOf: return "alignof";
  case Extension: return "__extension__";
  case OffsetOf: return "__builtin_offsetof";
  }
}

//===----------------------------------------------------------------------===//
// Postfix Operators.
//===----------------------------------------------------------------------===//

CallExpr::CallExpr(Expr *fn, Expr **args, unsigned numargs, QualType t,
                   SourceLocation rparenloc)
  : Expr(CallExprClass, t), NumArgs(numargs) {
  SubExprs = new Expr*[numargs+1];
  SubExprs[FN] = fn;
  for (unsigned i = 0; i != numargs; ++i)
    SubExprs[i+ARGS_START] = args[i];
  RParenLoc = rparenloc;
}

bool CallExpr::isBuiltinClassifyType(llvm::APSInt &Result) const {
  // The following enum mimics gcc's internal "typeclass.h" file.
  enum gcc_type_class {
    no_type_class = -1,
    void_type_class, integer_type_class, char_type_class,
    enumeral_type_class, boolean_type_class,
    pointer_type_class, reference_type_class, offset_type_class,
    real_type_class, complex_type_class,
    function_type_class, method_type_class,
    record_type_class, union_type_class,
    array_type_class, string_type_class,
    lang_type_class
  };
  Result.setIsSigned(true);
  
  // All simple function calls (e.g. func()) are implicitly cast to pointer to
  // function. As a result, we try and obtain the DeclRefExpr from the 
  // ImplicitCastExpr.
  const ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(getCallee());
  if (!ICE) // FIXME: deal with more complex calls (e.g. (func)(), (*func)()).
    return false;
  const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(ICE->getSubExpr());
  if (!DRE)
    return false;

  // We have a DeclRefExpr.
  if (strcmp(DRE->getDecl()->getName(), "__builtin_classify_type") == 0) {
    // If no argument was supplied, default to "no_type_class". This isn't 
    // ideal, however it's what gcc does.
    Result = static_cast<uint64_t>(no_type_class);
    if (NumArgs >= 1) {
      QualType argType = getArg(0)->getType();
      
      if (argType->isVoidType())
        Result = void_type_class;
      else if (argType->isEnumeralType())
        Result = enumeral_type_class;
      else if (argType->isBooleanType())
        Result = boolean_type_class;
      else if (argType->isCharType())
        Result = string_type_class; // gcc doesn't appear to use char_type_class
      else if (argType->isIntegerType())
        Result = integer_type_class;
      else if (argType->isPointerType())
        Result = pointer_type_class;
      else if (argType->isReferenceType())
        Result = reference_type_class;
      else if (argType->isRealType())
        Result = real_type_class;
      else if (argType->isComplexType())
        Result = complex_type_class;
      else if (argType->isFunctionType())
        Result = function_type_class;
      else if (argType->isStructureType())
        Result = record_type_class;
      else if (argType->isUnionType())
        Result = union_type_class;
      else if (argType->isArrayType())
        Result = array_type_class;
      else if (argType->isUnionType())
        Result = union_type_class;
      else  // FIXME: offset_type_class, method_type_class, & lang_type_class?
        assert(1 && "CallExpr::isBuiltinClassifyType(): unimplemented type");
    }
    return true;
  }
  return false;
}

/// getOpcodeStr - Turn an Opcode enum value into the punctuation char it
/// corresponds to, e.g. "<<=".
const char *BinaryOperator::getOpcodeStr(Opcode Op) {
  switch (Op) {
  default: assert(0 && "Unknown binary operator");
  case Mul:       return "*";
  case Div:       return "/";
  case Rem:       return "%";
  case Add:       return "+";
  case Sub:       return "-";
  case Shl:       return "<<";
  case Shr:       return ">>";
  case LT:        return "<";
  case GT:        return ">";
  case LE:        return "<=";
  case GE:        return ">=";
  case EQ:        return "==";
  case NE:        return "!=";
  case And:       return "&";
  case Xor:       return "^";
  case Or:        return "|";
  case LAnd:      return "&&";
  case LOr:       return "||";
  case Assign:    return "=";
  case MulAssign: return "*=";
  case DivAssign: return "/=";
  case RemAssign: return "%=";
  case AddAssign: return "+=";
  case SubAssign: return "-=";
  case ShlAssign: return "<<=";
  case ShrAssign: return ">>=";
  case AndAssign: return "&=";
  case XorAssign: return "^=";
  case OrAssign:  return "|=";
  case Comma:     return ",";
  }
}

InitListExpr::InitListExpr(SourceLocation lbraceloc, 
                           Expr **initexprs, unsigned numinits,
                           SourceLocation rbraceloc)
  : Expr(InitListExprClass, QualType())
  , NumInits(numinits)
  , LBraceLoc(lbraceloc)
  , RBraceLoc(rbraceloc)
{
  InitExprs = new Expr*[numinits];
  for (unsigned i = 0; i != numinits; i++)
    InitExprs[i] = initexprs[i];
}

//===----------------------------------------------------------------------===//
// Generic Expression Routines
//===----------------------------------------------------------------------===//

/// hasLocalSideEffect - Return true if this immediate expression has side
/// effects, not counting any sub-expressions.
bool Expr::hasLocalSideEffect() const {
  switch (getStmtClass()) {
  default:
    return false;
  case ParenExprClass:
    return cast<ParenExpr>(this)->getSubExpr()->hasLocalSideEffect();
  case UnaryOperatorClass: {
    const UnaryOperator *UO = cast<UnaryOperator>(this);
    
    switch (UO->getOpcode()) {
    default: return false;
    case UnaryOperator::PostInc:
    case UnaryOperator::PostDec:
    case UnaryOperator::PreInc:
    case UnaryOperator::PreDec:
      return true;                     // ++/--

    case UnaryOperator::Deref:
      // Dereferencing a volatile pointer is a side-effect.
      return getType().isVolatileQualified();
    case UnaryOperator::Real:
    case UnaryOperator::Imag:
      // accessing a piece of a volatile complex is a side-effect.
      return UO->getSubExpr()->getType().isVolatileQualified();

    case UnaryOperator::Extension:
      return UO->getSubExpr()->hasLocalSideEffect();
    }
  }
  case BinaryOperatorClass:
    return cast<BinaryOperator>(this)->isAssignmentOp();
  case CompoundAssignOperatorClass:
    return true;

  case MemberExprClass:
  case ArraySubscriptExprClass:
    // If the base pointer or element is to a volatile pointer/field, accessing
    // if is a side effect.
    return getType().isVolatileQualified();
    
  case CallExprClass:
    // TODO: check attributes for pure/const.   "void foo() { strlen("bar"); }"
    // should warn.
    return true;
    
  case CastExprClass:
    // If this is a cast to void, check the operand.  Otherwise, the result of
    // the cast is unused.
    if (getType()->isVoidType())
      return cast<CastExpr>(this)->getSubExpr()->hasLocalSideEffect();
    return false;
  }     
}

/// isLvalue - C99 6.3.2.1: an lvalue is an expression with an object type or an
/// incomplete type other than void. Nonarray expressions that can be lvalues:
///  - name, where name must be a variable
///  - e[i]
///  - (e), where e must be an lvalue
///  - e.name, where e must be an lvalue
///  - e->name
///  - *e, the type of e cannot be a function type
///  - string-constant
///  - reference type [C++ [expr]]
///
Expr::isLvalueResult Expr::isLvalue() const {
  // first, check the type (C99 6.3.2.1)
  if (TR->isFunctionType()) // from isObjectType()
    return LV_NotObjectType;

  if (TR->isVoidType())
    return LV_IncompleteVoidType;

  if (TR->isReferenceType()) // C++ [expr]
    return LV_Valid;

  // the type looks fine, now check the expression
  switch (getStmtClass()) {
  case StringLiteralClass: // C99 6.5.1p4
  case ArraySubscriptExprClass: // C99 6.5.3p4 (e1[e2] == (*((e1)+(e2))))
    // For vectors, make sure base is an lvalue (i.e. not a function call).
    if (cast<ArraySubscriptExpr>(this)->getBase()->getType()->isVectorType())
      return cast<ArraySubscriptExpr>(this)->getBase()->isLvalue();
    return LV_Valid;
  case DeclRefExprClass: // C99 6.5.1p2
    if (isa<VarDecl>(cast<DeclRefExpr>(this)->getDecl()))
      return LV_Valid;
    break;
  case MemberExprClass: { // C99 6.5.2.3p4
    const MemberExpr *m = cast<MemberExpr>(this);
    return m->isArrow() ? LV_Valid : m->getBase()->isLvalue();
  }
  case UnaryOperatorClass: // C99 6.5.3p4
    if (cast<UnaryOperator>(this)->getOpcode() == UnaryOperator::Deref)
      return LV_Valid;
    break;
  case ParenExprClass: // C99 6.5.1p5
    return cast<ParenExpr>(this)->getSubExpr()->isLvalue();
  case OCUVectorElementExprClass:
    if (cast<OCUVectorElementExpr>(this)->containsDuplicateElements())
      return LV_DuplicateVectorComponents;
    return LV_Valid;
  default:
    break;
  }
  return LV_InvalidExpression;
}

/// isModifiableLvalue - C99 6.3.2.1: an lvalue that does not have array type,
/// does not have an incomplete type, does not have a const-qualified type, and
/// if it is a structure or union, does not have any member (including, 
/// recursively, any member or element of all contained aggregates or unions)
/// with a const-qualified type.
Expr::isModifiableLvalueResult Expr::isModifiableLvalue() const {
  isLvalueResult lvalResult = isLvalue();
    
  switch (lvalResult) {
  case LV_Valid: break;
  case LV_NotObjectType: return MLV_NotObjectType;
  case LV_IncompleteVoidType: return MLV_IncompleteVoidType;
  case LV_DuplicateVectorComponents: return MLV_DuplicateVectorComponents;
  case LV_InvalidExpression: return MLV_InvalidExpression;
  }
  if (TR.isConstQualified())
    return MLV_ConstQualified;
  if (TR->isArrayType())
    return MLV_ArrayType;
  if (TR->isIncompleteType())
    return MLV_IncompleteType;
    
  if (const RecordType *r = dyn_cast<RecordType>(TR.getCanonicalType())) {
    if (r->hasConstFields()) 
      return MLV_ConstQualified;
  }
  return MLV_Valid;    
}

bool Expr::isConstantExpr(ASTContext &Ctx, SourceLocation *Loc) const {
  
  switch (getStmtClass()) {
  default:
    if (Loc) *Loc = getLocStart();
    return false;
  case ParenExprClass:
    return cast<ParenExpr>(this)->getSubExpr()->isConstantExpr(Ctx, Loc);
  case StringLiteralClass:
  case FloatingLiteralClass:
  case IntegerLiteralClass:
  case CharacterLiteralClass:
  case ImaginaryLiteralClass:
  case TypesCompatibleExprClass: 
    break;
  case CallExprClass: {
    const CallExpr *CE = cast<CallExpr>(this);
    llvm::APSInt Result(32);
    Result.zextOrTrunc(
      static_cast<uint32_t>(Ctx.getTypeSize(getType(), CE->getLocStart())));
    if (CE->isBuiltinClassifyType(Result))
      break;
    if (Loc) *Loc = getLocStart();
    return false;
  }
  case DeclRefExprClass:
    if (isa<EnumConstantDecl>(cast<DeclRefExpr>(this)->getDecl()))
      break;
    if (Loc) *Loc = getLocStart();
    return false;
  case UnaryOperatorClass: {
    const UnaryOperator *Exp = cast<UnaryOperator>(this);
    
    // Get the operand value.  If this is sizeof/alignof, do not evalute the
    // operand.  This affects C99 6.6p3.
    if (!Exp->isSizeOfAlignOfOp() &&
        !Exp->getSubExpr()->isConstantExpr(Ctx, Loc))
      return false;
  
    switch (Exp->getOpcode()) {
    // Address, indirect, pre/post inc/dec, etc are not valid constant exprs.
    // See C99 6.6p3.
    default:
      if (Loc) *Loc = Exp->getOperatorLoc();
      return false;
    case UnaryOperator::Extension:
      return true;  // FIXME: this is wrong.
    case UnaryOperator::SizeOf:
    case UnaryOperator::AlignOf:
      // sizeof(vla) is not a constantexpr: C99 6.5.3.4p2.
      if (!Exp->getSubExpr()->getType()->isConstantSizeType(Ctx, Loc))
        return false;
      break;
    case UnaryOperator::LNot:
    case UnaryOperator::Plus:
    case UnaryOperator::Minus:
    case UnaryOperator::Not:
      break;
    }
    break;
  }
  case SizeOfAlignOfTypeExprClass: {
    const SizeOfAlignOfTypeExpr *Exp = cast<SizeOfAlignOfTypeExpr>(this);
    // alignof always evaluates to a constant.
    if (Exp->isSizeOf() && !Exp->getArgumentType()->isConstantSizeType(Ctx,Loc))
      return false;
    break;
  }
  case BinaryOperatorClass: {
    const BinaryOperator *Exp = cast<BinaryOperator>(this);
    
    // The LHS of a constant expr is always evaluated and needed.
    if (!Exp->getLHS()->isConstantExpr(Ctx, Loc))
      return false;

    if (!Exp->getRHS()->isConstantExpr(Ctx, Loc))
      return false;
    
    break;
  }
  case ImplicitCastExprClass:
  case CastExprClass: {
    const Expr *SubExpr;
    SourceLocation CastLoc;
    if (const CastExpr *C = dyn_cast<CastExpr>(this)) {
      SubExpr = C->getSubExpr();
      CastLoc = C->getLParenLoc();
    } else {
      SubExpr = cast<ImplicitCastExpr>(this)->getSubExpr();
      CastLoc = getLocStart();
    }
    if (!SubExpr->isConstantExpr(Ctx, Loc)) {
      if (Loc) *Loc = SubExpr->getLocStart();
      return false;
    }
    break;
  }
  case ConditionalOperatorClass: {
    const ConditionalOperator *Exp = cast<ConditionalOperator>(this);
    
    if (!Exp->getCond()->isConstantExpr(Ctx, Loc))
      return false;

    if (!Exp->getLHS()->isConstantExpr(Ctx, Loc))
      return false;

    if (!Exp->getRHS()->isConstantExpr(Ctx, Loc))
      return false;
    break;
  }
  }

  return true;
}

/// isIntegerConstantExpr - this recursive routine will test if an expression is
/// an integer constant expression. Note: With the introduction of VLA's in
/// C99 the result of the sizeof operator is no longer always a constant
/// expression. The generalization of the wording to include any subexpression
/// that is not evaluated (C99 6.6p3) means that nonconstant subexpressions
/// can appear as operands to other operators (e.g. &&, ||, ?:). For instance,
/// "0 || f()" can be treated as a constant expression. In C90 this expression,
/// occurring in a context requiring a constant, would have been a constraint
/// violation. FIXME: This routine currently implements C90 semantics.
/// To properly implement C99 semantics this routine will need to evaluate
/// expressions involving operators previously mentioned.

/// FIXME: Pass up a reason why! Invalid operation in i-c-e, division by zero,
/// comma, etc
///
/// FIXME: This should ext-warn on overflow during evaluation!  ISO C does not
/// permit this.  This includes things like (int)1e1000
///
/// FIXME: Handle offsetof.  Two things to do:  Handle GCC's __builtin_offsetof
/// to support gcc 4.0+  and handle the idiom GCC recognizes with a null pointer
/// cast+dereference.
bool Expr::isIntegerConstantExpr(llvm::APSInt &Result, ASTContext &Ctx,
                                 SourceLocation *Loc, bool isEvaluated) const {
  switch (getStmtClass()) {
  default:
    if (Loc) *Loc = getLocStart();
    return false;
  case ParenExprClass:
    return cast<ParenExpr>(this)->getSubExpr()->
                     isIntegerConstantExpr(Result, Ctx, Loc, isEvaluated);
  case IntegerLiteralClass:
    Result = cast<IntegerLiteral>(this)->getValue();
    break;
  case CharacterLiteralClass: {
    const CharacterLiteral *CL = cast<CharacterLiteral>(this);
    Result.zextOrTrunc(
      static_cast<uint32_t>(Ctx.getTypeSize(getType(), CL->getLoc())));
    Result = CL->getValue();
    Result.setIsUnsigned(!getType()->isSignedIntegerType());
    break;
  }
  case TypesCompatibleExprClass: {
    const TypesCompatibleExpr *TCE = cast<TypesCompatibleExpr>(this);
    Result.zextOrTrunc(
      static_cast<uint32_t>(Ctx.getTypeSize(getType(), TCE->getLocStart())));
    Result = TCE->typesAreCompatible();
    break;
  }
  case CallExprClass: {
    const CallExpr *CE = cast<CallExpr>(this);
    Result.zextOrTrunc(
      static_cast<uint32_t>(Ctx.getTypeSize(getType(), CE->getLocStart())));
    if (CE->isBuiltinClassifyType(Result))
      break;
    if (Loc) *Loc = getLocStart();
    return false;
  }
  case DeclRefExprClass:
    if (const EnumConstantDecl *D = 
          dyn_cast<EnumConstantDecl>(cast<DeclRefExpr>(this)->getDecl())) {
      Result = D->getInitVal();
      break;
    }
    if (Loc) *Loc = getLocStart();
    return false;
  case UnaryOperatorClass: {
    const UnaryOperator *Exp = cast<UnaryOperator>(this);
    
    // Get the operand value.  If this is sizeof/alignof, do not evalute the
    // operand.  This affects C99 6.6p3.
    if (!Exp->isSizeOfAlignOfOp() &&
        !Exp->getSubExpr()->isIntegerConstantExpr(Result, Ctx, Loc,isEvaluated))
      return false;

    switch (Exp->getOpcode()) {
    // Address, indirect, pre/post inc/dec, etc are not valid constant exprs.
    // See C99 6.6p3.
    default:
      if (Loc) *Loc = Exp->getOperatorLoc();
      return false;
    case UnaryOperator::Extension:
      return true;  // FIXME: this is wrong.
    case UnaryOperator::SizeOf:
    case UnaryOperator::AlignOf:
      // sizeof(vla) is not a constantexpr: C99 6.5.3.4p2.
      if (!Exp->getSubExpr()->getType()->isConstantSizeType(Ctx, Loc))
        return false;
      
      // Return the result in the right width.
      Result.zextOrTrunc(
        static_cast<uint32_t>(Ctx.getTypeSize(getType(),
                                              Exp->getOperatorLoc())));

      // Get information about the size or align.
      if (Exp->getOpcode() == UnaryOperator::SizeOf)
        Result = Ctx.getTypeSize(Exp->getSubExpr()->getType(),
                                 Exp->getOperatorLoc());
      else
        Result = Ctx.getTypeAlign(Exp->getSubExpr()->getType(),
                                  Exp->getOperatorLoc());
      break;
    case UnaryOperator::LNot: {
      bool Val = Result != 0;
      Result.zextOrTrunc(
        static_cast<uint32_t>(Ctx.getTypeSize(getType(),
                                              Exp->getOperatorLoc())));
      Result = Val;
      break;
    }
    case UnaryOperator::Plus:
      break;
    case UnaryOperator::Minus:
      Result = -Result;
      break;
    case UnaryOperator::Not:
      Result = ~Result;
      break;
    }
    break;
  }
  case SizeOfAlignOfTypeExprClass: {
    const SizeOfAlignOfTypeExpr *Exp = cast<SizeOfAlignOfTypeExpr>(this);
    // alignof always evaluates to a constant.
    if (Exp->isSizeOf() && !Exp->getArgumentType()->isConstantSizeType(Ctx,Loc))
      return false;

    // Return the result in the right width.
    Result.zextOrTrunc(
      static_cast<uint32_t>(Ctx.getTypeSize(getType(), Exp->getOperatorLoc())));
    
    // Get information about the size or align.
    if (Exp->isSizeOf())
      Result = Ctx.getTypeSize(Exp->getArgumentType(), Exp->getOperatorLoc());
    else
      Result = Ctx.getTypeAlign(Exp->getArgumentType(), Exp->getOperatorLoc());
    break;
  }
  case BinaryOperatorClass: {
    const BinaryOperator *Exp = cast<BinaryOperator>(this);
    
    // The LHS of a constant expr is always evaluated and needed.
    if (!Exp->getLHS()->isIntegerConstantExpr(Result, Ctx, Loc, isEvaluated))
      return false;
    
    llvm::APSInt RHS(Result);
    
    // The short-circuiting &&/|| operators don't necessarily evaluate their
    // RHS.  Make sure to pass isEvaluated down correctly.
    if (Exp->isLogicalOp()) {
      bool RHSEval;
      if (Exp->getOpcode() == BinaryOperator::LAnd)
        RHSEval = Result != 0;
      else {
        assert(Exp->getOpcode() == BinaryOperator::LOr &&"Unexpected logical");
        RHSEval = Result == 0;
      }
      
      if (!Exp->getRHS()->isIntegerConstantExpr(RHS, Ctx, Loc,
                                                isEvaluated & RHSEval))
        return false;
    } else {
      if (!Exp->getRHS()->isIntegerConstantExpr(RHS, Ctx, Loc, isEvaluated))
        return false;
    }
    
    switch (Exp->getOpcode()) {
    default:
      if (Loc) *Loc = getLocStart();
      return false;
    case BinaryOperator::Mul:
      Result *= RHS;
      break;
    case BinaryOperator::Div:
      if (RHS == 0) {
        if (!isEvaluated) break;
        if (Loc) *Loc = getLocStart();
        return false;
      }
      Result /= RHS;
      break;
    case BinaryOperator::Rem:
      if (RHS == 0) {
        if (!isEvaluated) break;
        if (Loc) *Loc = getLocStart();
        return false;
      }
      Result %= RHS;
      break;
    case BinaryOperator::Add: Result += RHS; break;
    case BinaryOperator::Sub: Result -= RHS; break;
    case BinaryOperator::Shl:
      Result <<= 
        static_cast<uint32_t>(RHS.getLimitedValue(Result.getBitWidth()-1));
      break;
    case BinaryOperator::Shr:
      Result >>= 
        static_cast<uint32_t>(RHS.getLimitedValue(Result.getBitWidth()-1));
      break;
    case BinaryOperator::LT:  Result = Result < RHS; break;
    case BinaryOperator::GT:  Result = Result > RHS; break;
    case BinaryOperator::LE:  Result = Result <= RHS; break;
    case BinaryOperator::GE:  Result = Result >= RHS; break;
    case BinaryOperator::EQ:  Result = Result == RHS; break;
    case BinaryOperator::NE:  Result = Result != RHS; break;
    case BinaryOperator::And: Result &= RHS; break;
    case BinaryOperator::Xor: Result ^= RHS; break;
    case BinaryOperator::Or:  Result |= RHS; break;
    case BinaryOperator::LAnd:
      Result = Result != 0 && RHS != 0;
      break;
    case BinaryOperator::LOr:
      Result = Result != 0 || RHS != 0;
      break;
      
    case BinaryOperator::Comma:
      // C99 6.6p3: "shall not contain assignment, ..., or comma operators,
      // *except* when they are contained within a subexpression that is not
      // evaluated".  Note that Assignment can never happen due to constraints
      // on the LHS subexpr, so we don't need to check it here.
      if (isEvaluated) {
        if (Loc) *Loc = getLocStart();
        return false;
      }
      
      // The result of the constant expr is the RHS.
      Result = RHS;
      return true;
    }
    
    assert(!Exp->isAssignmentOp() && "LHS can't be a constant expr!");
    break;
  }
  case ImplicitCastExprClass:
  case CastExprClass: {
    const Expr *SubExpr;
    SourceLocation CastLoc;
    if (const CastExpr *C = dyn_cast<CastExpr>(this)) {
      SubExpr = C->getSubExpr();
      CastLoc = C->getLParenLoc();
    } else {
      SubExpr = cast<ImplicitCastExpr>(this)->getSubExpr();
      CastLoc = getLocStart();
    }
    
    // C99 6.6p6: shall only convert arithmetic types to integer types.
    if (!SubExpr->getType()->isArithmeticType() ||
        !getType()->isIntegerType()) {
      if (Loc) *Loc = SubExpr->getLocStart();
      return false;
    }

    uint32_t DestWidth = 
      static_cast<uint32_t>(Ctx.getTypeSize(getType(), CastLoc));
    
    // Handle simple integer->integer casts.
    if (SubExpr->getType()->isIntegerType()) {
      if (!SubExpr->isIntegerConstantExpr(Result, Ctx, Loc, isEvaluated))
        return false;
      
      // Figure out if this is a truncate, extend or noop cast.
      // If the input is signed, do a sign extend, noop, or truncate.
      if (SubExpr->getType()->isSignedIntegerType())
        Result.sextOrTrunc(DestWidth);
      else  // If the input is unsigned, do a zero extend, noop, or truncate.
        Result.zextOrTrunc(DestWidth);
      break;
    }
    
    // Allow floating constants that are the immediate operands of casts or that
    // are parenthesized.
    const Expr *Operand = SubExpr;
    while (const ParenExpr *PE = dyn_cast<ParenExpr>(Operand))
      Operand = PE->getSubExpr();

    // If this isn't a floating literal, we can't handle it.
    const FloatingLiteral *FL = dyn_cast<FloatingLiteral>(Operand);
    if (!FL) {
      if (Loc) *Loc = Operand->getLocStart();
      return false;
    }
    
    // Determine whether we are converting to unsigned or signed.
    bool DestSigned = getType()->isSignedIntegerType();

    // TODO: Warn on overflow, but probably not here: isIntegerConstantExpr can
    // be called multiple times per AST.
    uint64_t Space[4]; 
    (void)FL->getValue().convertToInteger(Space, DestWidth, DestSigned,
                                          llvm::APFloat::rmTowardZero);
    Result = llvm::APInt(DestWidth, 4, Space);
    break;
  }
  case ConditionalOperatorClass: {
    const ConditionalOperator *Exp = cast<ConditionalOperator>(this);
    
    if (!Exp->getCond()->isIntegerConstantExpr(Result, Ctx, Loc, isEvaluated))
      return false;
    
    const Expr *TrueExp  = Exp->getLHS();
    const Expr *FalseExp = Exp->getRHS();
    if (Result == 0) std::swap(TrueExp, FalseExp);
    
    // Evaluate the false one first, discard the result.
    if (!FalseExp->isIntegerConstantExpr(Result, Ctx, Loc, false))
      return false;
    // Evalute the true one, capture the result.
    if (!TrueExp->isIntegerConstantExpr(Result, Ctx, Loc, isEvaluated))
      return false;
    break;
  }
  }

  // Cases that are valid constant exprs fall through to here.
  Result.setIsUnsigned(getType()->isUnsignedIntegerType());
  return true;
}


/// isNullPointerConstant - C99 6.3.2.3p3 -  Return true if this is either an
/// integer constant expression with the value zero, or if this is one that is
/// cast to void*.
bool Expr::isNullPointerConstant(ASTContext &Ctx) const {
  // Strip off a cast to void*, if it exists.
  if (const CastExpr *CE = dyn_cast<CastExpr>(this)) {
    // Check that it is a cast to void*.
    if (const PointerType *PT = dyn_cast<PointerType>(CE->getType())) {
      QualType Pointee = PT->getPointeeType();
      if (Pointee.getQualifiers() == 0 && Pointee->isVoidType() && // to void*
          CE->getSubExpr()->getType()->isIntegerType())            // from int.
        return CE->getSubExpr()->isNullPointerConstant(Ctx);
    }
  } else if (const ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(this)) {
    // Ignore the ImplicitCastExpr type entirely.
    return ICE->getSubExpr()->isNullPointerConstant(Ctx);
  } else if (const ParenExpr *PE = dyn_cast<ParenExpr>(this)) {
    // Accept ((void*)0) as a null pointer constant, as many other
    // implementations do.
    return PE->getSubExpr()->isNullPointerConstant(Ctx);
  }
  
  // This expression must be an integer type.
  if (!getType()->isIntegerType())
    return false;
  
  // If we have an integer constant expression, we need to *evaluate* it and
  // test for the value 0.
  llvm::APSInt Val(32);
  return isIntegerConstantExpr(Val, Ctx, 0, true) && Val == 0;
}

unsigned OCUVectorElementExpr::getNumElements() const {
  return strlen(Accessor.getName());
}


/// getComponentType - Determine whether the components of this access are
/// "point" "color" or "texture" elements.
OCUVectorElementExpr::ElementType 
OCUVectorElementExpr::getElementType() const {
  // derive the component type, no need to waste space.
  const char *compStr = Accessor.getName();
  
  if (OCUVectorType::getPointAccessorIdx(*compStr) != -1) return Point;
  if (OCUVectorType::getColorAccessorIdx(*compStr) != -1) return Color;
  
  assert(OCUVectorType::getTextureAccessorIdx(*compStr) != -1 &&
         "getComponentType(): Illegal accessor");
  return Texture;
}

/// containsDuplicateElements - Return true if any element access is
/// repeated.
bool OCUVectorElementExpr::containsDuplicateElements() const {
  const char *compStr = Accessor.getName();
  unsigned length = strlen(compStr);
  
  for (unsigned i = 0; i < length-1; i++) {
    const char *s = compStr+i;
    for (const char c = *s++; *s; s++)
      if (c == *s) 
        return true;
  }
  return false;
}

/// getEncodedElementAccess - We encode fields with two bits per component.
unsigned OCUVectorElementExpr::getEncodedElementAccess() const {
  const char *compStr = Accessor.getName();
  unsigned length = getNumElements();

  unsigned Result = 0;
  
  while (length--) {
    Result <<= 2;
    int Idx = OCUVectorType::getAccessorIdx(compStr[length]);
    assert(Idx != -1 && "Invalid accessor letter");
    Result |= Idx;
  }
  return Result;
}

// constructor for unary messages.
ObjCMessageExpr::ObjCMessageExpr(
  IdentifierInfo *clsName, IdentifierInfo &methName, QualType retType, 
  SourceLocation LBrac, SourceLocation RBrac)
  : Expr(ObjCMessageExprClass, retType), Selector(methName) {
  ClassName = clsName;
  LBracloc = LBrac;
  RBracloc = RBrac;
}

ObjCMessageExpr::ObjCMessageExpr(
  Expr *fn, IdentifierInfo &methName, QualType retType, 
  SourceLocation LBrac, SourceLocation RBrac)
  : Expr(ObjCMessageExprClass, retType), Selector(methName), ClassName(0) {
  SubExprs = new Expr*[1];
  SubExprs[RECEIVER] = fn;
  LBracloc = LBrac;
  RBracloc = RBrac;
}

// constructor for keyword messages.
ObjCMessageExpr::ObjCMessageExpr(
  Expr *fn, IdentifierInfo &selInfo, ObjcKeywordMessage *keys, unsigned numargs, 
  QualType retType, SourceLocation LBrac, SourceLocation RBrac)
  : Expr(ObjCMessageExprClass, retType), Selector(selInfo), ClassName(0) {
  SubExprs = new Expr*[numargs+1];
  SubExprs[RECEIVER] = fn;
  for (unsigned i = 0; i != numargs; ++i)
    SubExprs[i+ARGS_START] = static_cast<Expr *>(keys[i].KeywordExpr);
  LBracloc = LBrac;
  RBracloc = RBrac;
}

ObjCMessageExpr::ObjCMessageExpr(
  IdentifierInfo *clsName, IdentifierInfo &selInfo, ObjcKeywordMessage *keys, 
  unsigned numargs, QualType retType, SourceLocation LBrac, SourceLocation RBrac)
  : Expr(ObjCMessageExprClass, retType), Selector(selInfo), ClassName(clsName) {
  SubExprs = new Expr*[numargs+1];
  SubExprs[RECEIVER] = 0;
  for (unsigned i = 0; i != numargs; ++i)
    SubExprs[i+ARGS_START] = static_cast<Expr *>(keys[i].KeywordExpr);
  LBracloc = LBrac;
  RBracloc = RBrac;
}


//===----------------------------------------------------------------------===//
//  Child Iterators for iterating over subexpressions/substatements
//===----------------------------------------------------------------------===//

// DeclRefExpr
Stmt::child_iterator DeclRefExpr::child_begin() { return NULL; }
Stmt::child_iterator DeclRefExpr::child_end() { return NULL; }

// PreDefinedExpr
Stmt::child_iterator PreDefinedExpr::child_begin() { return NULL; }
Stmt::child_iterator PreDefinedExpr::child_end() { return NULL; }

// IntegerLiteral
Stmt::child_iterator IntegerLiteral::child_begin() { return NULL; }
Stmt::child_iterator IntegerLiteral::child_end() { return NULL; }

// CharacterLiteral
Stmt::child_iterator CharacterLiteral::child_begin() { return NULL; }
Stmt::child_iterator CharacterLiteral::child_end() { return NULL; }

// FloatingLiteral
Stmt::child_iterator FloatingLiteral::child_begin() { return NULL; }
Stmt::child_iterator FloatingLiteral::child_end() { return NULL; }

// ImaginaryLiteral
Stmt::child_iterator ImaginaryLiteral::child_begin() {
  return reinterpret_cast<Stmt**>(&Val);
}
Stmt::child_iterator ImaginaryLiteral::child_end() {
  return reinterpret_cast<Stmt**>(&Val)+1;
}

// StringLiteral
Stmt::child_iterator StringLiteral::child_begin() { return NULL; }
Stmt::child_iterator StringLiteral::child_end() { return NULL; }

// ParenExpr
Stmt::child_iterator ParenExpr::child_begin() {
  return reinterpret_cast<Stmt**>(&Val);
}
Stmt::child_iterator ParenExpr::child_end() {
  return reinterpret_cast<Stmt**>(&Val)+1;
}

// UnaryOperator
Stmt::child_iterator UnaryOperator::child_begin() {
  return reinterpret_cast<Stmt**>(&Val);
}
Stmt::child_iterator UnaryOperator::child_end() {
  return reinterpret_cast<Stmt**>(&Val)+1;
}

// SizeOfAlignOfTypeExpr
Stmt::child_iterator SizeOfAlignOfTypeExpr::child_begin() { return NULL; }
Stmt::child_iterator SizeOfAlignOfTypeExpr::child_end() { return NULL; }

// ArraySubscriptExpr
Stmt::child_iterator ArraySubscriptExpr::child_begin() {
  return reinterpret_cast<Stmt**>(&SubExprs);
}
Stmt::child_iterator ArraySubscriptExpr::child_end() {
  return reinterpret_cast<Stmt**>(&SubExprs)+END_EXPR;
}

// CallExpr
Stmt::child_iterator CallExpr::child_begin() {
  return reinterpret_cast<Stmt**>(&SubExprs[0]);
}
Stmt::child_iterator CallExpr::child_end() {
  return reinterpret_cast<Stmt**>(&SubExprs[NumArgs+ARGS_START]);
}

// MemberExpr
Stmt::child_iterator MemberExpr::child_begin() {
  return reinterpret_cast<Stmt**>(&Base);
}
Stmt::child_iterator MemberExpr::child_end() {
  return reinterpret_cast<Stmt**>(&Base)+1;
}

// OCUVectorElementExpr
Stmt::child_iterator OCUVectorElementExpr::child_begin() {
  return reinterpret_cast<Stmt**>(&Base);
}
Stmt::child_iterator OCUVectorElementExpr::child_end() {
  return reinterpret_cast<Stmt**>(&Base)+1;
}

// CompoundLiteralExpr
Stmt::child_iterator CompoundLiteralExpr::child_begin() {
  return reinterpret_cast<Stmt**>(&Init);
}
Stmt::child_iterator CompoundLiteralExpr::child_end() {
  return reinterpret_cast<Stmt**>(&Init)+1;
}

// ImplicitCastExpr
Stmt::child_iterator ImplicitCastExpr::child_begin() {
  return reinterpret_cast<Stmt**>(&Op);
}
Stmt::child_iterator ImplicitCastExpr::child_end() {
  return reinterpret_cast<Stmt**>(&Op)+1;
}

// CastExpr
Stmt::child_iterator CastExpr::child_begin() {
  return reinterpret_cast<Stmt**>(&Op);
}
Stmt::child_iterator CastExpr::child_end() {
  return reinterpret_cast<Stmt**>(&Op)+1;
}

// BinaryOperator
Stmt::child_iterator BinaryOperator::child_begin() {
  return reinterpret_cast<Stmt**>(&SubExprs);
}
Stmt::child_iterator BinaryOperator::child_end() {
  return reinterpret_cast<Stmt**>(&SubExprs)+END_EXPR;
}

// ConditionalOperator
Stmt::child_iterator ConditionalOperator::child_begin() {
  return reinterpret_cast<Stmt**>(&SubExprs);
}
Stmt::child_iterator ConditionalOperator::child_end() {
  return reinterpret_cast<Stmt**>(&SubExprs)+END_EXPR;
}

// AddrLabelExpr
Stmt::child_iterator AddrLabelExpr::child_begin() { return NULL; }
Stmt::child_iterator AddrLabelExpr::child_end() { return NULL; }

// StmtExpr
Stmt::child_iterator StmtExpr::child_begin() {
  return reinterpret_cast<Stmt**>(&SubStmt);
}
Stmt::child_iterator StmtExpr::child_end() {
  return reinterpret_cast<Stmt**>(&SubStmt)+1;
}

// TypesCompatibleExpr
Stmt::child_iterator TypesCompatibleExpr::child_begin() { return NULL; }
Stmt::child_iterator TypesCompatibleExpr::child_end() { return NULL; }

// ChooseExpr
Stmt::child_iterator ChooseExpr::child_begin() {
  return reinterpret_cast<Stmt**>(&SubExprs);
}

Stmt::child_iterator ChooseExpr::child_end() {
  return reinterpret_cast<Stmt**>(&SubExprs)+END_EXPR;
}

// InitListExpr
Stmt::child_iterator InitListExpr::child_begin() {
  return reinterpret_cast<Stmt**>(&InitExprs[0]);
}
Stmt::child_iterator InitListExpr::child_end() {
  return reinterpret_cast<Stmt**>(&InitExprs[NumInits]);
}

// ObjCStringLiteral
Stmt::child_iterator ObjCStringLiteral::child_begin() { return NULL; }
Stmt::child_iterator ObjCStringLiteral::child_end() { return NULL; }

// ObjCEncodeExpr
Stmt::child_iterator ObjCEncodeExpr::child_begin() { return NULL; }
Stmt::child_iterator ObjCEncodeExpr::child_end() { return NULL; }

// ObjCMessageExpr
Stmt::child_iterator ObjCMessageExpr::child_begin() {
  return reinterpret_cast<Stmt**>(&SubExprs[0]);
}
Stmt::child_iterator ObjCMessageExpr::child_end() {
  return reinterpret_cast<Stmt**>(&SubExprs[NumArgs+ARGS_START]);
}

