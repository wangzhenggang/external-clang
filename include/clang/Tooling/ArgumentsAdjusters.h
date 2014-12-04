//===--- ArgumentsAdjusters.h - Command line arguments adjuster -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares base abstract class ArgumentsAdjuster and its descendants.
// These classes are intended to modify command line arguments obtained from
// a compilation database before they are used to run a frontend action.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLING_ARGUMENTSADJUSTERS_H
#define LLVM_CLANG_TOOLING_ARGUMENTSADJUSTERS_H

#include <string>
#include <vector>

namespace clang {

namespace tooling {

/// \brief A sequence of command line arguments.
typedef std::vector<std::string> CommandLineArguments;

/// \brief Abstract interface for a command line adjusters.
///
/// This abstract interface describes a command line argument adjuster,
/// which is responsible for command line arguments modification before
/// the arguments are used to run a frontend action.
class ArgumentsAdjuster {
  virtual void anchor();
public:
  /// \brief Returns adjusted command line arguments.
  ///
  /// \param Args Input sequence of command line arguments.
  ///
  /// \returns Modified sequence of command line arguments.
  virtual CommandLineArguments Adjust(const CommandLineArguments &Args) = 0;
  virtual ~ArgumentsAdjuster() {}
};

/// \brief Syntax check only command line adjuster.
///
/// This class implements ArgumentsAdjuster interface and converts input
/// command line arguments to the "syntax check only" variant.
class ClangSyntaxOnlyAdjuster : public ArgumentsAdjuster {
  CommandLineArguments Adjust(const CommandLineArguments &Args) override;
};

/// \brief An argument adjuster which removes output-related command line
/// arguments.
class ClangStripOutputAdjuster : public ArgumentsAdjuster {
  CommandLineArguments Adjust(const CommandLineArguments &Args) override;
};

class InsertArgumentAdjuster : public ArgumentsAdjuster {
public:
  enum Position { BEGIN, END };

  InsertArgumentAdjuster(const CommandLineArguments &Extra, Position Pos)
      : Extra(Extra), Pos(Pos) {}

  InsertArgumentAdjuster(const char *Extra, Position Pos)
      : Extra(1, std::string(Extra)), Pos(Pos) {}

  CommandLineArguments Adjust(const CommandLineArguments &Args) override;

private:
  const CommandLineArguments Extra;
  const Position Pos;
};
} // end namespace tooling
} // end namespace clang

#endif // LLVM_CLANG_TOOLING_ARGUMENTSADJUSTERS_H

