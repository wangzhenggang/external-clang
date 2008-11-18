//===--- ExprCXX.cpp - (C++) Expression AST Node Implementation -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the subclesses of Expr class declared in ExprCXX.h
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/IdentifierTable.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/ExprCXX.h"
using namespace clang;

void CXXConditionDeclExpr::Destroy(ASTContext& C) {
  getVarDecl()->Destroy(C);
  delete this;
}


//===----------------------------------------------------------------------===//
//  Child Iterators for iterating over subexpressions/substatements
//===----------------------------------------------------------------------===//

// CXXTypeidExpr - has child iterators if the operand is an expression
Stmt::child_iterator CXXTypeidExpr::child_begin() {
  return isTypeOperand() ? child_iterator() : (Stmt**)&Operand;
}
Stmt::child_iterator CXXTypeidExpr::child_end() {
  return isTypeOperand() ? child_iterator() : (Stmt**)&Operand+1;
}

// CXXBoolLiteralExpr
Stmt::child_iterator CXXBoolLiteralExpr::child_begin() { 
  return child_iterator();
}
Stmt::child_iterator CXXBoolLiteralExpr::child_end() {
  return child_iterator();
}

// CXXThisExpr
Stmt::child_iterator CXXThisExpr::child_begin() { return child_iterator(); }
Stmt::child_iterator CXXThisExpr::child_end() { return child_iterator(); }

// CXXThrowExpr
Stmt::child_iterator CXXThrowExpr::child_begin() { return &Op; }
Stmt::child_iterator CXXThrowExpr::child_end() {
  // If Op is 0, we are processing throw; which has no children.
  return Op ? &Op+1 : &Op;
}

// CXXDefaultArgExpr
Stmt::child_iterator CXXDefaultArgExpr::child_begin() {
  return child_iterator();
}
Stmt::child_iterator CXXDefaultArgExpr::child_end() {
  return child_iterator();
}

// CXXZeroInitValueExpr
Stmt::child_iterator CXXZeroInitValueExpr::child_begin() { 
  return child_iterator();
}
Stmt::child_iterator CXXZeroInitValueExpr::child_end() {
  return child_iterator();
}

// CXXConditionDeclExpr
Stmt::child_iterator CXXConditionDeclExpr::child_begin() {
  return getVarDecl();
}
Stmt::child_iterator CXXConditionDeclExpr::child_end() {
  return child_iterator();
}

OverloadedOperatorKind CXXOperatorCallExpr::getOperator() const {
  // All simple function calls (e.g. func()) are implicitly cast to pointer to
  // function. As a result, we try and obtain the DeclRefExpr from the 
  // ImplicitCastExpr.
  const ImplicitCastExpr *ICE = dyn_cast<ImplicitCastExpr>(getCallee());
  if (!ICE) // FIXME: deal with more complex calls (e.g. (func)(), (*func)()).
    return OO_None;
  
  const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(ICE->getSubExpr());
  if (!DRE)
    return OO_None;
  
  if (const FunctionDecl *FDecl = dyn_cast<FunctionDecl>(DRE->getDecl()))
    return FDecl->getDeclName().getCXXOverloadedOperator();  
  else if (const OverloadedFunctionDecl *Ovl 
             = dyn_cast<OverloadedFunctionDecl>(DRE->getDecl()))
    return Ovl->getDeclName().getCXXOverloadedOperator();
  else
    return OO_None;
}

SourceRange CXXOperatorCallExpr::getSourceRange() const {
  OverloadedOperatorKind Kind = getOperator();
  if (Kind == OO_PlusPlus || Kind == OO_MinusMinus) {
    if (getNumArgs() == 1)
      // Prefix operator
      return SourceRange(getOperatorLoc(), 
                         getArg(0)->getSourceRange().getEnd());
    else
      // Postfix operator
      return SourceRange(getArg(0)->getSourceRange().getEnd(),
                         getOperatorLoc());
  } else if (Kind == OO_Call) {
    return SourceRange(getArg(0)->getSourceRange().getBegin(), getRParenLoc());
  } else if (Kind == OO_Subscript) {
    return SourceRange(getArg(0)->getSourceRange().getBegin(), getRParenLoc());
  } else if (getNumArgs() == 1) {
    return SourceRange(getOperatorLoc(), getArg(0)->getSourceRange().getEnd());
  } else if (getNumArgs() == 2) {
    return SourceRange(getArg(0)->getSourceRange().getBegin(),
                       getArg(1)->getSourceRange().getEnd());
  } else {
    return SourceRange();
  }
}

//===----------------------------------------------------------------------===//
//  Named casts
//===----------------------------------------------------------------------===//

/// getCastName - Get the name of the C++ cast being used, e.g.,
/// "static_cast", "dynamic_cast", "reinterpret_cast", or
/// "const_cast". The returned pointer must not be freed.
const char *CXXNamedCastExpr::getCastName() const {
  switch (getStmtClass()) {
  case CXXStaticCastExprClass:      return "static_cast";
  case CXXDynamicCastExprClass:     return "dynamic_cast";
  case CXXReinterpretCastExprClass: return "reinterpret_cast";
  case CXXConstCastExprClass:       return "const_cast";
  default:                          return "<invalid cast>";
  }
}
