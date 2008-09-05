//===--- Decl.cpp - Declaration AST Node Implementation -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the Decl subclasses.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/Decl.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Stmt.h"
#include "clang/Basic/IdentifierTable.h"

using namespace clang;

//===----------------------------------------------------------------------===//
// Decl Allocation/Deallocation Method Implementations
//===----------------------------------------------------------------------===//
 
TranslationUnitDecl *TranslationUnitDecl::Create(ASTContext &C) {
  void *Mem = C.getAllocator().Allocate<TranslationUnitDecl>();
  return new (Mem) TranslationUnitDecl();
}

NamespaceDecl *NamespaceDecl::Create(ASTContext &C, DeclContext *DC,
                                     SourceLocation L, IdentifierInfo *Id) {
  void *Mem = C.getAllocator().Allocate<NamespaceDecl>();
  return new (Mem) NamespaceDecl(DC, L, Id);
}

void NamespaceDecl::Destroy(ASTContext& C) {
  // NamespaceDecl uses "NextDeclarator" to chain namespace declarations
  // together. They are all top-level Decls.
  
  this->~NamespaceDecl();
  C.getAllocator().Deallocate((void *)this);
}


ImplicitParamDecl *ImplicitParamDecl::Create(ASTContext &C, DeclContext *DC,
    SourceLocation L, IdentifierInfo *Id, QualType T, ScopedDecl *PrevDecl) {
  void *Mem = C.getAllocator().Allocate<ImplicitParamDecl>();
  return new (Mem) ImplicitParamDecl(ImplicitParam, DC, L, Id, T, PrevDecl);
}

VarDecl *VarDecl::Create(ASTContext &C, DeclContext *DC,
                         SourceLocation L,
                         IdentifierInfo *Id, QualType T,
                         StorageClass S, ScopedDecl *PrevDecl) {
  void *Mem = C.getAllocator().Allocate<VarDecl>();
  return new (Mem) VarDecl(Var, DC, L, Id, T, S, PrevDecl);
}

ParmVarDecl *ParmVarDecl::Create(ASTContext &C, DeclContext *DC,
                                 SourceLocation L, IdentifierInfo *Id,
                                 QualType T, StorageClass S,
                                 Expr *DefArg, ScopedDecl *PrevDecl) {
  void *Mem = C.getAllocator().Allocate<ParmVarDecl>();
  return new (Mem) ParmVarDecl(DC, L, Id, T, S, DefArg, PrevDecl);
}

FunctionDecl *FunctionDecl::Create(ASTContext &C, DeclContext *DC,
                                   SourceLocation L, 
                                   IdentifierInfo *Id, QualType T, 
                                   StorageClass S, bool isInline, 
                                   ScopedDecl *PrevDecl) {
  void *Mem = C.getAllocator().Allocate<FunctionDecl>();
  return new (Mem) FunctionDecl(Function, DC, L, Id, T, S, isInline, PrevDecl);
}

FieldDecl *FieldDecl::Create(ASTContext &C, SourceLocation L,
                             IdentifierInfo *Id, QualType T, Expr *BW) {
  void *Mem = C.getAllocator().Allocate<FieldDecl>();
  return new (Mem) FieldDecl(L, Id, T, BW);
}


EnumConstantDecl *EnumConstantDecl::Create(ASTContext &C, EnumDecl *CD,
                                           SourceLocation L,
                                           IdentifierInfo *Id, QualType T,
                                           Expr *E, const llvm::APSInt &V, 
                                           ScopedDecl *PrevDecl){
  void *Mem = C.getAllocator().Allocate<EnumConstantDecl>();
  return new (Mem) EnumConstantDecl(CD, L, Id, T, E, V, PrevDecl);
}

void EnumConstantDecl::Destroy(ASTContext& C) {
  if (Init) Init->Destroy(C);
  Decl::Destroy(C);
}

TypedefDecl *TypedefDecl::Create(ASTContext &C, DeclContext *DC,
                                 SourceLocation L,
                                 IdentifierInfo *Id, QualType T,
                                 ScopedDecl *PD) {
  void *Mem = C.getAllocator().Allocate<TypedefDecl>();
  return new (Mem) TypedefDecl(DC, L, Id, T, PD);
}

EnumDecl *EnumDecl::Create(ASTContext &C, DeclContext *DC, SourceLocation L,
                           IdentifierInfo *Id,
                           ScopedDecl *PrevDecl) {
  void *Mem = C.getAllocator().Allocate<EnumDecl>();
  return new (Mem) EnumDecl(DC, L, Id, PrevDecl);
}

void EnumDecl::Destroy(ASTContext& C) {
  if (getEnumConstantList()) getEnumConstantList()->Destroy(C);
  Decl::Destroy(C);
}

FileScopeAsmDecl *FileScopeAsmDecl::Create(ASTContext &C,
                                           SourceLocation L,
                                           StringLiteral *Str) {
  void *Mem = C.getAllocator().Allocate<FileScopeAsmDecl>();
  return new (Mem) FileScopeAsmDecl(L, Str);
}

LinkageSpecDecl *LinkageSpecDecl::Create(ASTContext &C,
                                         SourceLocation L,
                                         LanguageIDs Lang, Decl *D) {
  void *Mem = C.getAllocator().Allocate<LinkageSpecDecl>();
  return new (Mem) LinkageSpecDecl(L, Lang, D);
}

//===----------------------------------------------------------------------===//
// NamedDecl Implementation
//===----------------------------------------------------------------------===//

const char *NamedDecl::getName() const {
  if (const IdentifierInfo *II = getIdentifier())
    return II->getName();
  return "";
}

//===----------------------------------------------------------------------===//
// FunctionDecl Implementation
//===----------------------------------------------------------------------===//

FunctionDecl::~FunctionDecl() {
  delete[] ParamInfo;
}

void FunctionDecl::Destroy(ASTContext& C) {
  if (Body)
    Body->Destroy(C);

  for (param_iterator I=param_begin(), E=param_end(); I!=E; ++I)
    (*I)->Destroy(C);
    
  Decl::Destroy(C);
}


Stmt *FunctionDecl::getBody(const FunctionDecl *&Definition) const {
  for (const FunctionDecl *FD = this; FD != 0; FD = FD->PreviousDeclaration) {
    if (FD->Body) {
      Definition = FD;
      return FD->Body;
    }
  }

  return 0;
}

unsigned FunctionDecl::getNumParams() const {
  const FunctionType *FT = getType()->getAsFunctionType();
  if (isa<FunctionTypeNoProto>(FT))
    return 0;
  return cast<FunctionTypeProto>(FT)->getNumArgs();
}

void FunctionDecl::setParams(ParmVarDecl **NewParamInfo, unsigned NumParams) {
  assert(ParamInfo == 0 && "Already has param info!");
  assert(NumParams == getNumParams() && "Parameter count mismatch!");
  
  // Zero params -> null pointer.
  if (NumParams) {
    ParamInfo = new ParmVarDecl*[NumParams];
    memcpy(ParamInfo, NewParamInfo, sizeof(ParmVarDecl*)*NumParams);
  }
}

/// getMinRequiredArguments - Returns the minimum number of arguments
/// needed to call this function. This may be fewer than the number of
/// function parameters, if some of the parameters have default
/// arguments (in C++).
unsigned FunctionDecl::getMinRequiredArguments() const {
  unsigned NumRequiredArgs = getNumParams();
  while (NumRequiredArgs > 0
         && getParamDecl(NumRequiredArgs-1)->getDefaultArg())
    --NumRequiredArgs;

  return NumRequiredArgs;
}

//===----------------------------------------------------------------------===//
// RecordDecl Implementation
//===----------------------------------------------------------------------===//

RecordDecl::RecordDecl(Kind DK, DeclContext *DC, SourceLocation L,
                       IdentifierInfo *Id)
: TagDecl(DK, DC, L, Id, 0) {
  
  HasFlexibleArrayMember = false;
  assert(classof(static_cast<Decl*>(this)) && "Invalid Kind!");
  Members = 0;
  NumMembers = -1;  
}

RecordDecl *RecordDecl::Create(ASTContext &C, TagKind TK, DeclContext *DC,
                               SourceLocation L, IdentifierInfo *Id) {
  
  void *Mem = C.getAllocator().Allocate<RecordDecl>();
  Kind DK;
  switch (TK) {
    default: assert(0 && "Invalid TagKind!");
    case TK_enum: assert(0 && "Enum TagKind passed for Record!");
    case TK_struct: DK = Struct; break;
    case TK_union:  DK = Union;  break;
    case TK_class:  DK = Class;  break;
  }
  return new (Mem) RecordDecl(DK, DC, L, Id);
}

RecordDecl::~RecordDecl() {
  delete[] Members;
}

void RecordDecl::Destroy(ASTContext& C) {
  if (isDefinition())
    for (field_iterator I=field_begin(), E=field_end(); I!=E; ++I)
      (*I)->Destroy(C);

  TagDecl::Destroy(C);
}

/// defineBody - When created, RecordDecl's correspond to a forward declared
/// record.  This method is used to mark the decl as being defined, with the
/// specified contents.
void RecordDecl::defineBody(FieldDecl **members, unsigned numMembers) {
  assert(!isDefinition() && "Cannot redefine record!");
  setDefinition(true);
  NumMembers = numMembers;
  if (numMembers) {
    Members = new FieldDecl*[numMembers];
    memcpy(Members, members, numMembers*sizeof(Decl*));
  }
}

FieldDecl *RecordDecl::getMember(IdentifierInfo *II) {
  if (Members == 0 || NumMembers < 0)
    return 0;
  
  // Linear search.  When C++ classes come along, will likely need to revisit.
  for (int i = 0; i != NumMembers; ++i)
    if (Members[i]->getIdentifier() == II)
      return Members[i];
  return 0;
}
