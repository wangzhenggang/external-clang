//===--- Builtins.h - Builtin function header -------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines enum values for all the target-independent builtin
// functions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_BUILTINS_H
#define LLVM_CLANG_AST_BUILTINS_H

#include <cstring>
#include <string>

namespace clang {
  class TargetInfo;
  class IdentifierTable;
  class ASTContext;
  class QualType;

namespace Builtin {
enum ID {
  NotBuiltin  = 0,      // This is not a builtin function.
#define BUILTIN(ID, TYPE, ATTRS) BI##ID,
#include "clang/AST/Builtins.def"
  FirstTSBuiltin
};

struct Info {
  const char *Name, *Type, *Attributes, *HeaderName;
  bool Suppressed;

  bool operator==(const Info &RHS) const {
    return !strcmp(Name, RHS.Name) &&
           !strcmp(Type, RHS.Type) &&
           !strcmp(Attributes, RHS.Attributes);
  }
  bool operator!=(const Info &RHS) const { return !(*this == RHS); }
};

/// Builtin::Context - This holds information about target-independent and
/// target-specific builtins, allowing easy queries by clients.
class Context {
  const Info *TSRecords;
  unsigned NumTSRecords;
public:
  Context() : TSRecords(0), NumTSRecords(0) {}

  /// \brief Load all of the target builtins. This should be called
  /// prior to initializing the builtin identifiers.
  void InitializeTargetBuiltins(const TargetInfo &Target);

  /// InitializeBuiltins - Mark the identifiers for all the builtins with their
  /// appropriate builtin ID # and mark any non-portable builtin identifiers as
  /// such.
  void InitializeBuiltins(IdentifierTable &Table, bool NoBuiltins = false);
  
  /// Builtin::GetName - Return the identifier name for the specified builtin,
  /// e.g. "__builtin_abs".
  const char *GetName(unsigned ID) const {
    return GetRecord(ID).Name;
  }
  
  /// isConst - Return true if this function has no side effects and doesn't
  /// read memory.
  bool isConst(unsigned ID) const {
    return strchr(GetRecord(ID).Attributes, 'c') != 0;
  }
  
  /// isNoThrow - Return true if we know this builtin never throws an exception.
  bool isNoThrow(unsigned ID) const {
    return strchr(GetRecord(ID).Attributes, 'n') != 0;
  }
  
  /// isLibFunction - Return true if this is a builtin for a libc/libm function,
  /// with a "__builtin_" prefix (e.g. __builtin_abs).
  bool isLibFunction(unsigned ID) const {
    return strchr(GetRecord(ID).Attributes, 'F') != 0;
  }
  
  /// \brief Determines whether this builtin is a predefined libc/libm
  /// function, such as "malloc", where we know the signature a
  /// priori.
  bool isPredefinedLibFunction(unsigned ID) const {
    return strchr(GetRecord(ID).Attributes, 'f') != 0;
  }

  /// \brief If this is a library function that comes from a specific
  /// header, retrieve that header name.
  const char *getHeaderName(unsigned ID) const {
    return GetRecord(ID).HeaderName;
  }

  /// \brief Determine whether this builtin is like printf in its
  /// formatting rules and, if so, set the index to the format string
  /// argument and whether this function as a va_list argument.
  bool isPrintfLike(unsigned ID, unsigned &FormatIdx, bool &HasVAListArg);

  /// hasVAListUse - Return true of the specified builtin uses __builtin_va_list
  /// as an operand or return type.
  bool hasVAListUse(unsigned ID) const {
    return strpbrk(GetRecord(ID).Type, "Aa") != 0;
  }
  
  /// isConstWithoutErrno - Return true if this function has no side
  /// effects and doesn't read memory, except for possibly errno. Such
  /// functions can be const when the MathErrno lang option is
  /// disabled.
  bool isConstWithoutErrno(unsigned ID) const {
    return strchr(GetRecord(ID).Attributes, 'e') != 0;
  }

  /// GetBuiltinType - Return the type for the specified builtin.
  enum GetBuiltinTypeError {
    GE_None, //< No error
    GE_Missing_FILE //< Missing the FILE type from <stdio.h>
  };
  QualType GetBuiltinType(unsigned ID, ASTContext &Context,
                          GetBuiltinTypeError &Error) const;
private:
  const Info &GetRecord(unsigned ID) const;
};

}
} // end namespace clang
#endif
